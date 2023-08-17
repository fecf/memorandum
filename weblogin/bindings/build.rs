fn main() {
  windows::build!(
      Windows::Foundation::*,
      Windows::Globalization::Language,
      Windows::Storage::*,
      Windows::Storage::Streams::*,
      Windows::Graphics::Imaging::*,
      Windows::Media::Ocr::*,
  );
}