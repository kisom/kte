{
  description = "kyle's text editor";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = inputs @ { self, nixpkgs, ... }:
    let
      eachSystem = nixpkgs.lib.genAttrs nixpkgs.lib.systems.flakeExposed;
      pkgsFor = system: import nixpkgs { inherit system; };
    in {
      packages = eachSystem (system: {
        default = (pkgsFor system).callPackage ./default-nogui.nix { };
        kge     = (pkgsFor system).callPackage ./default-gui.nix   { };
        kte     = (pkgsFor system).callPackage ./default-nogui.nix { };
        full    = (pkgsFor system).callPackage ./default.nix       { };
      });
    };
}