export default function (callback: ((durationMs: number, stack: string | null) => void), options: {
    threshold: number;
    interval: number;
}): void;
