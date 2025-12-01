{
  description = "Kyle's Text Editor";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs?ref=nixos-unstable";
  };

  outputs =
    { self, nixpkgs }:
    let
      pkgs = import nixpkgs { system = "x86_64-linux"; };
    in
    {
      packages.x86_64-linux = {
        default = pkgs.callPackage ./default-nogui.nix { };
        kge     = pkgs.callPackage ./default-gui.nix   { };
        kte     = pkgs.callPackage ./default-nogui.nix { };
        full    = pkgs.callPackage ./default.nix       { };
      };
    };
}
