# flake.nix
{
  description = "kte  ImGui/SDL2 text editor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
      in {
        packages.default = pkgs.stdenv.mkDerivation {
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

          # Alternative (even stronger): completely hide the broken module
          preConfigure = ''
            # If the project ships its own FindSDL2.cmake in cmake/, hide it
            if [ -f cmake/FindSDL2.cmake ]; then
              mv cmake/FindSDL2.cmake cmake/FindSDL2.cmake.disabled
              echo "Disabled bundled FindSDL2.cmake"
            fi
          '';

          meta = with pkgs.lib; {
            description = "kte  ImGui/SDL2 GUI editor";
            mainProgram = "kte";
            platforms = platforms.linux;
          };
        };

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];
          packages = with pkgs; [ gdb clang-tools ];
        };
      });
}