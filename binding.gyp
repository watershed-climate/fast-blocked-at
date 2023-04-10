{
  "targets": [
    {
      "target_name": "addon",
      "sources": [ "native.cc" ],
      "cflags_cc": ["-std=c++2a"],
      "conditions": [
        ["OS=='mac'", {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-std=c++2a"]
          }
        }],
      ],
    },
  ],
}
