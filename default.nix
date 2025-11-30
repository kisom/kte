# default.nix
let
  pkgs = import <nixpkgs> {};
in
pkgs.stdenv.mkDerivation {
  pname = "kte";
  version = "0.1.0";

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
}
