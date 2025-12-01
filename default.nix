{
  lib,
  stdenv,
  cmake,
  ncurses,
  SDL2,
  libGL,
  xorg,
  installShellFiles,
  ...
}:
let
  cmakeContent = builtins.readFile ./CMakeLists.txt;
  cmakeLines = lib.splitString "\n" cmakeContent;
  versionLine = lib.findFirst (l: builtins.match ".*set\\(KTE_VERSION \".+\"\\).*" l != null) (throw "KTE_VERSION not found in CMakeLists.txt") cmakeLines;
  version = builtins.head (builtins.match ".*set\\(KTE_VERSION \"(.+)\"\\).*" versionLine);
in
stdenv.mkDerivation {
  pname = "kte";
  inherit version;

  src = lib.cleanSource ./.;

  nativeBuildInputs = [
    cmake
    ncurses
    SDL2
    libGL
    xorg.libX11
    installShellFiles
  ];

  cmakeFlags = [
    "-DBUILD_GUI=ON"
    "-DCMAKE_BUILD_TYPE=Debug"
  ];

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp kte $out/bin/
    cp kge $out/bin/

    installManPage ../docs/kte.1
    installManPage ../docs/kge.1

    runHook postInstall
  '';
}
