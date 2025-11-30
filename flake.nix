# flake.nix
{
  description = "kte – a small terminal text editor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in
        {
          packages.default = pkgs.stdenv.mkDerivation {
            pname = "kte";
            version = "0.1.0";  # change if you have a version

            src = ./.;

            nativeBuildInputs = [ pkgs.cmake ];
            buildInputs = with pkgs; [
              ncurses   # provides libncursesw.so + headers via .dev output
            ];

            # Crucial: tell CMake to use wide ncurses and prefer pkg-config
            cmakeFlags = [
              "-DCURSES_NEED_NCURSES=TRUE"
              "-DCURSES_NEED_WIDE=TRUE"
              "-DUSE_PKGCONFIG=ON"
            ];

            # Optional: make it obvious in the build log
            preConfigure = ''
            echo "Using ncurses from ${pkgs.ncurses}"
            echo "lib dir: ${pkgs.ncurses}/lib"
            echo "include dir: ${pkgs.ncurses.dev}/include"
          '';

            meta = with pkgs.lib; {
              description = "A small terminal text editor";
              license = licenses.mit;  # change if needed
              platforms = platforms.linux;
              mainProgram = "kte";
            };
          };

          devShells.default = pkgs.mkShell {
            buildInputs = with pkgs; [
              cmake
              gdb
              clang-tools  # for clangd, clang-format, etc.
              ncurses
            ];

            # Makes ncurses visible to every tool (including manual cmake runs)
            hardeningDisable = [ "all" ];  # optional, only if you hit fortify issues
          };
        });
}
