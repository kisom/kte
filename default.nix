{
  lib,
  stdenv,
  cmake,
  installShellFiles,
  ...
}:
let
  cmakeContent = builtins.readFile ./CMakeLists.txt;
  cmakeLines = lib.splitString "\n" cmakeContent;
  versionLine = lib.findFirst (l: builtins.match ".*set\\(KTE_VERSION \".+\"\\).*" l
  version = builtins.head (builtins.match ".*set\\(KTE_VERSION \"(.+)\"\\).*" versio
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
    "-DKTE_USE_PIECE_TABLE=ON"
    "-DCURSES_NEED_NCURSES=TRUE"
    "-DCURSES_NEED_WIDE=TRUE"
  ];

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp kte $out/bin/

    installManPage ../docs/kte.1

    runHook postInstall
  '';
}
