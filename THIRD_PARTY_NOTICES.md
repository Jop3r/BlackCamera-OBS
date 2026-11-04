# Third-party components

The plugin is distributed together with the following components. Their
licenses apply to those components only; the plugin source code is MIT-licensed
(see `LICENSE`).

| Component | License | Source |
|-----------|---------|--------|
| OpenCV 4.x (and the libraries its DLLs bundle: protobuf, abseil, zlib) | Apache-2.0 (OpenCV); BSD / zlib style licenses for the bundled libraries | https://github.com/opencv/opencv |
| YuNet face detection model (`face_detection_yunet_2023mar.onnx`) | MIT | https://github.com/opencv/opencv_zoo/tree/main/models/face_detection_yunet |
| libobs headers (build time only, from `tools/obs-sdk.ps1`) | GPL-2.0-or-later | https://github.com/obsproject/obs-studio |

The plugin links against libobs (OBS Studio, GPL-2.0-or-later). A binary of the
plugin is a combined work with OBS and is distributed under the terms of the GPL;
the MIT license of the source code is compatible with that.

The full license texts of the vcpkg-built libraries are in
`vcpkg_installed/x64-windows/share/<package>/copyright` of a CMake build.
