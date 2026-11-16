/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <node.h>
#include <optional>
#include <string>
#include <thread>
#include <uv.h>

using namespace std::chrono_literals;
using namespace v8;

/**
 * Appends a V8 string to a std::u16string.
 */
void AppendV8String(Isolate *isolate, std::u16string &r, Local<String> s) {
  if (s.IsEmpty()) {
    r += u"(unknown)";
    return;
  }
  std::size_t sz = r.size();
  r.resize(sz + s->Length());
  s->Write(isolate, (uint16_t *)(&r[sz]));
}

/**
 * Appends an integer to a std::u16string.
 */
void AppendNumber(std::u16string &r, int num) {
  std::string s = std::to_string(num);
  for (const auto &ch : s) {
    r.push_back((char16_t)ch);
  }
}

// Comments starting with ^ indicate PlusCal code, and are extracted by the
// build script.
//
//^ This is a formal proof that this code does not deadlock.
//^ In particular, there are multiple threads of execution:
//^ `^\begin{enumerate}
//^     \item The "watchdog" thread, which listens for heartbeats and schedules
//^     a Javascript interrupt if it has been too long since a heartbeat.
//^
//^     \item The "javascript" thread, which executes user's JavaScript and
//^     runs the interrupts.
//^
//^     \item Other threads, which may terminate the JavaScript thread.
//^ \end{enumerate}^'
//^
//^ ---- MODULE EventLoopBlockage ----
//^ EXTENDS TLC
//^ (*--algorithm Poller {
//^ variables
//^  request_interrupt_begin = FALSE;
//^  fork_watchdog = FALSE;
//^  watchdog_joined = FALSE;
class PerIsolateData {
public:
  explicit PerIsolateData(Isolate *isolate)
      : //^ should_stop = FALSE;
        should_stop_(false),
        //^ missed_heartbeat = FALSE;
        last_heartbeat_(std::chrono::steady_clock::now().time_since_epoch()),
        //^ interrupt_done = FALSE;
        interrupt_done_(false),
        //^ stack_ready = FALSE;
        stack_ready_(false), isolate_(isolate) {
    //^ begin_terminate = FALSE;
    node::AddEnvironmentCleanupHook(isolate, DeleteInstance, this);
  }

  //^ process (watchdog = "watchdog") {
  /**
   * Begins the watchdog. The watchdog waits and enqueues an interrupt if there
   * is no heartbeat from the isolate for longer than threshold_ms milliseconds.
   */
  bool StartWatchdog(Local<Function> callback, uint64_t interval_ms,
                     uint64_t threshold_ms) {
    //^ WatchdogWaitingFork:
    //^ await fork_watchdog \/ should_stop;
    if (watchdog_) {
      return false;
    }
    callback_.Reset(isolate_, callback);
    interval_ms_ = interval_ms;
    threshold_ms_ = threshold_ms;

    watchdog_ = std::thread([this]() {
      //^ WatchdogStart:
      //^ while (~should_stop) {
      while (!should_stop_) {
        //^ MarkHeartbeatMissed:
        //^ missed_heartbeat := TRUE;
        std::this_thread::sleep_for(threshold_ms_ * 1ms);

        //^ CheckHeartbeatMissed:
        //^ if (~missed_heartbeat) {
        //^   skip;
        //^ } else {
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        auto last_heartbeat = last_heartbeat_.load();
        uint64_t estimated_event_loop_blockage_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_heartbeat)
                .count();
        if (estimated_event_loop_blockage_ms <= threshold_ms_) {
          continue;
        }

        //^ MarkInterruptNotDone:
        //^ interrupt_done := FALSE;
        interrupt_done_ = false;

        //^ RequestInterruptBeginProfiler:
        //^ request_interrupt_begin := TRUE;
        // TODO(keyhan): should we be using env->RequestInterrupt?
        isolate_->RequestInterrupt(BeginInterruptCallback, this);

        //^ CheckShouldStopBeforeBegin1:
        //^ if (should_stop) {
        //^   goto WatchdogEnd;
        //^ };
        if (should_stop_) {
          return;
        }

        //^ WaitUntilBeginProfiler:
        //^ await interrupt_done;
        {
          std::unique_lock<std::mutex> lk(interrupt_done_m_);
          interrupt_done_cv_.wait(lk, [this] { return !!interrupt_done_; });
        }

        //^ MarkStackReady:
        //^ stack_ready := TRUE;
        stack_ready_ = true;

        //^ CheckShouldStopBeforeBegin2:
        //^ if (should_stop) {
        //^   goto WatchdogEnd;
        //^ };
        if (should_stop_) {
          return;
        }

        //^ WaitUntilStackNotReady:
        //^ await ~stack_ready;
        {
          std::unique_lock<std::mutex> lk(stack_ready_m_);
          stack_ready_cv_.wait(lk, [this] { return !stack_ready_; });
        }
        //^ }
        //^ };
      }
      //^ WatchdogEnd:
      //^   watchdog_joined := TRUE;
      //^ }
    });
    return true;
  }

  //^ process (javascript = "javascript") {
  //^   Executing:
  //^   while (~begin_terminate) {
  //^    either {
  //^      ForkWatchdog:
  //^        fork_watchdog := TRUE;
  //^    } or {
  /**
   * Heartbeat and record the time it occurred.  If there is a stack pending,
   * then a recent heartbeat was late, and we invoke callback_ with the
   * blockage duration and the stack from the interrupt.
   */
  void Heartbeat() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    //^ CheckForStack:
    //^ if (stack_ready) {
    if (stack_ready_) {
      uint64_t estimated_event_loop_blockage_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              now - last_heartbeat_.load())
              .count();

      // In expectation, the delay will "begin" halfway through the polling
      // cycle. Subtract off this number to compute it correctly.
      // (Note there is still some overestimation, as other timers may execute
      // before the heartbeat does.)
      uint64_t bias = interval_ms_ / 2;
      if (estimated_event_loop_blockage_ms < bias) {
        estimated_event_loop_blockage_ms = 0;
      } else {
        estimated_event_loop_blockage_ms -= bias;
      }

      MaybeLocal<String> maybe_stack = String::NewFromTwoByte(
          isolate_, (uint16_t *)captured_stack_trace_.data(),
          NewStringType::kNormal, captured_stack_trace_.length());
      // Clear the captured stack so the next interrupt can use it.
      captured_stack_trace_.clear();

      Local<Value> stack;
      if (!maybe_stack.ToLocal(&stack)) {
        // This is probably impossible as it would mean the string exceeded the
        // string size limit, but handle it gracefully anyway.
        stack = Null(isolate_);
      }

      Local<Number> blockage_ms =
          Number::New(isolate_, estimated_event_loop_blockage_ms);

      Local<Value> argv[2] = {blockage_ms, stack};
      Local<Context> context = isolate_->GetCurrentContext();
      // Ignore the function results.
      // TODO(kvakil): should we throw an error?
      (void)(Local<Function>::New(isolate_, callback_)
                 ->Call(context, Null(isolate_), 2, argv));

      //^ MarkStackReceived:
      //^ stack_ready := FALSE;
      stack_ready_ = false;
      stack_ready_cv_.notify_one();
      //^ };
    }
    //^ Heartbeat:
    //^ missed_heartbeat := FALSE;
    last_heartbeat_ = now;
    //^ } or {
  }

private:
  //^ InterruptBegin:
  /**
   * Captures the isolate's current stack trace and formats it as a UTF16
   * string. Queues the stack string up to be consumed by Heartbeat.
   */
  void BeginInterrupt() {
    //^ if (request_interrupt_begin) {
    //^ request_interrupt_begin := FALSE;
    Local<StackTrace> stack_trace = StackTrace::CurrentStackTrace(isolate_, 32);
    int frame_count = stack_trace->GetFrameCount();
    for (int i = 0; i < frame_count; i++) {
      captured_stack_trace_ += u"    at ";

      Local<StackFrame> stack_frame = stack_trace->GetFrame(isolate_, i);

      if (stack_frame->IsConstructor()) {
        captured_stack_trace_ += u"new ";
      }

      AppendV8String(isolate_, captured_stack_trace_,
                     stack_frame->GetFunctionName());

      captured_stack_trace_ += u" ";

      if (stack_frame->IsWasm()) {
        captured_stack_trace_ += u"<WASM> ";
      }

      captured_stack_trace_ += u"(";

      if (stack_frame->IsEval()) {
        captured_stack_trace_ += u"[eval]";
      } else {
        AppendV8String(isolate_, captured_stack_trace_,
                       stack_frame->GetScriptName());
      }
      captured_stack_trace_ += u":";

      auto line_number = stack_frame->GetLineNumber();
      if (line_number != Message::kNoLineNumberInfo) {
        AppendNumber(captured_stack_trace_, line_number);
      } else {
        captured_stack_trace_ += u"(unknown)";
      }

      captured_stack_trace_ += u":";
      auto column_number = stack_frame->GetColumn();
      if (column_number != Message::kNoColumnInfo) {
        AppendNumber(captured_stack_trace_, column_number);
      } else {
        captured_stack_trace_ += u"(unknown)";
      }

      captured_stack_trace_ += u")";
      if (i != frame_count - 1) {
        captured_stack_trace_ += u"\n";
      }
    }
    //^ interrupt_done := TRUE;
    interrupt_done_ = true;
    interrupt_done_cv_.notify_one();
    //^ }
    //^ } or {
  }

  ~PerIsolateData() {
    //^ BeginTermination:
    //^   begin_terminate := TRUE;
    //^ }
    //^ };
    if (!watchdog_) {
      return;
    }

    //^ DoTerminate:
    //^ should_stop := TRUE;
    should_stop_ = true;

    //^ DoTerminate2:
    //^ interrupt_done := TRUE;
    interrupt_done_ = true;
    interrupt_done_cv_.notify_one();

    //^ DoTerminate3:
    //^ stack_ready := FALSE;
    stack_ready_ = false;
    stack_ready_cv_.notify_one();

    //^ WaitWatchdog:
    //^ await watchdog_joined;
    watchdog_->join();
    //^ }
  }
  //^ } *)
  //^ ====

  static void DeleteInstance(void *data) {
    delete static_cast<PerIsolateData *>(data);
  }

  static void BeginInterruptCallback(Isolate *isolate, void *data) {
    static_cast<PerIsolateData *>(data)->BeginInterrupt();
  }

  std::u16string captured_stack_trace_;

  std::optional<std::thread> watchdog_;

  uint64_t interval_ms_;
  uint64_t threshold_ms_;

  std::atomic<bool> should_stop_;

  std::atomic<std::chrono::steady_clock::duration> last_heartbeat_;

  std::condition_variable interrupt_done_cv_;
  std::mutex interrupt_done_m_;
  std::atomic<bool> interrupt_done_;

  std::condition_variable stack_ready_cv_;
  std::mutex stack_ready_m_;
  std::atomic<bool> stack_ready_;

  Isolate *isolate_;
  Persistent<Function> callback_;
};

static void StartWatchdog(const FunctionCallbackInfo<Value> &info) {
  PerIsolateData *data =
      reinterpret_cast<PerIsolateData *>(info.Data().As<External>()->Value());
  if (!info[0]->IsFunction() || !info[1]->IsNumber() || !info[2]->IsNumber()) {
    info.GetIsolate()->ThrowException(Exception::TypeError(
        String::NewFromUtf8(info.GetIsolate(), "bad arguments")
            .ToLocalChecked()));
    return;
  }
  Local<Function> f = info[0].As<Function>();
  uint64_t interval_ms = info[1].As<Number>()->Value();
  uint64_t threshold_ms = info[2].As<Number>()->Value();
  info.GetReturnValue().Set(data->StartWatchdog(f, interval_ms, threshold_ms));
}

static void Heartbeat(const FunctionCallbackInfo<Value> &info) {
  PerIsolateData *data =
      reinterpret_cast<PerIsolateData *>(info.Data().As<External>()->Value());
  data->Heartbeat();
}

NODE_MODULE_INIT() {
  Isolate *isolate = context->GetIsolate();

  PerIsolateData *data = new PerIsolateData(isolate);

  Local<External> external = External::New(isolate, data);

  exports
      ->Set(context, String::NewFromUtf8(isolate, "heartbeat").ToLocalChecked(),
            FunctionTemplate::New(isolate, Heartbeat, external)
                ->GetFunction(context)
                .ToLocalChecked())
      .FromJust();
  exports
      ->Set(context,
            String::NewFromUtf8(isolate, "startWatchdog").ToLocalChecked(),
            FunctionTemplate::New(isolate, StartWatchdog, external)
                ->GetFunction(context)
                .ToLocalChecked())
      .FromJust();
  // TODO(kvakil): implement stopWatchdog. This is tricky, since the Javascript
  // thread may (theoretically?) have an interrupt scheduled while we stop.
}
