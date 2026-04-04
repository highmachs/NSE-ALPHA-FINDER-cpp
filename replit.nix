{pkgs}: {
  deps = [
    pkgs.python312Packages.uvicorn
    pkgs.python312Packages.fastapi
    pkgs.cmake
    pkgs.python312Packages.pybind11
    pkgs.python312
  ];
}
