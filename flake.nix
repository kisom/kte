{
  description = "kyle's text editor";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs, ... }:
    let
      eachSystem = nixpkgs.lib.genAttrs nixpkgs.lib.systems.flakeExposed;
      pkgsFor = system: import nixpkgs { inherit system; };
    in
    {
      packages = eachSystem (system: rec {
        default = kte;
        full = kge;
        kte = (pkgsFor system).callPackage ./default.nix { graphical = false; };
        kge = (pkgsFor system).callPackage ./default.nix { graphical = true; };
      });

      devShells = eachSystem (system:
        let pkgs = pkgsFor system;
        in {
          default = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.kge ];
            packages = with pkgs; [ gdb valgrind ];
          };
          terminal = pkgs.mkShell {
            inputsFrom = [ self.packages.${system}.kte ];
          };
        }
      );

      overlays.default = final: prev: {
        kte = self.packages.${final.system}.kte;
        kge = self.packages.${final.system}.kge;
      };
    };
}
