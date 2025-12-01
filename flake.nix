{
  description = "kyle's text editor";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs =
    inputs@{ self, nixpkgs, ... }:
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
    };
}
