from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def main() -> None:
    epub_reader = read("src/activities/reader/EpubReaderActivity.cpp")
    gray_block = epub_reader[
        epub_reader.index("const bool needsGrayscale = enableTextAA || enableImageGrayscaleOnly"):
        epub_reader.index("renderer.displayGrayBuffer();")
    ]
    if gray_block.count("renderStatusBar();") < 3:
        raise AssertionError(
            "EPUB grayscale rendering must include the status bar in the tiled path and both fallback buffers"
        )


if __name__ == "__main__":
    main()
