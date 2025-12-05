{
  lib,
  stdenv,
  cmake,
  ncurses,
  SDL2,
  libGL,
  xorg,
  installShellFiles,

  graphical ? false,
  graphical-qt ? false,
  ...
}:
let
  cmakeContent = builtins.readFile ./CMakeLists.txt;
  cmakeLines = lib.splitString "\n" cmakeContent;
  versionLine = lib.findFirst (
    l: builtins.match ".*set\\(KTE_VERSION \".+\"\\).*" l != null
  ) (throw "KTE_VERSION not found in CMakeLists.txt") cmakeLines;
  version = builtins.head (builtins.match ".*set\\(KTE_VERSION \"(.+)\"\\).*" versionLine);
in
stdenv.mkDerivation {
  pname = "kte";
  inherit version;

  src = lib.cleanSource ./.;

  nativeBuildInputs = [
    cmake
    ncurses
    installShellFiles
  ]
  ++ lib.optionals graphical [
    SDL2
    libGL
    xorg.libX11
  ]
  ++ lib.optionals graphical-qt [
    qt5Full
    qtcreator ## not sure if this is actually needed
  ];

  cmakeFlags = [
    "-DBUILD_GUI=${if graphical or graphical-qt then "ON" else "OFF"}"
    "-DKTE_USE_QT=${if graphical-qt then "ON" else "OFF"}"
    "-DCMAKE_BUILD_TYPE=Debug"
  ];

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp kte $out/bin/

    installManPage ../docs/kte.1

  ''
  + lib.optionalString graphical ''
    cp kge $out/bin/
    installManPage ../docs/kge.1
    mkdir -p $out/share/icons
    cp ../kge.png $out/share/icons/
  ''
  + ''
    runHook postInstall
  '';
}
