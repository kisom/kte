{
  lib,
  stdenv,
  cmake,
  libGL,
  SDL2,
  xorg.libX11
  installShellFiles,
  ...
}:
let
  cmakeContent = builtins.readFile ./CMakeLists.txt;
  cmakeLines = lib.splitString "\n" cmakeContent;
  versionLine = lib.findFirst (l: builtins.match ".*set\\(KE_VERSION \".+\"\\).*" l
  version = builtins.head (builtins.match ".*set\\(KE_VERSION \"(.+)\"\\).*" versio
in
pkgs.stdenv.mkDerivation {
  pname = "kte";
  inherit version;

  src = ./.;

  nativeBuildInputs = [ pkgs.cmake pkgs.pkg-config ];
  buildInputs = with pkgs; [
    ncurses
    SDL2
    libGL
    xorg.libX11
  ];

  cmakeFlags = [
    "-DBUILD_GUI=ON"
    "-DCURSES_NEED_NCURSES=TRUE"
    "-DCURSES_NEED_WIDE=TRUE"
  ];

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp kte $out/bin/
    cp kge $out/bin/

    installManPage ../docs/kte.1
    installManPage ../docs/kte.1

    runHook postInstall
  '';
}
