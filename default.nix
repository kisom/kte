# default.nix
let
  pkgs = import <nixpkgs> {};
in
pkgs.stdenv.mkDerivation rec {
  pname = "kte";
  version = "0.1.0";

  src = ./.;

  nativeBuildInputs = [ pkgs.cmake ];
  buildInputs = [ pkgs.ncurses ];

  cmakeFlags = [
    "-DCURSES_NEED_NCURSES=TRUE"
    "-DCURSES_NEED_WIDE=TRUE"
  ];

  meta = with pkgs.lib; {
    description = "A small terminal text editor";
    license = licenses.mit;
    platforms = platforms.linux;
    mainProgram = "kte";
  };
}
