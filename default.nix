{
  lib,
  stdenv,
  cmake,
  ncurses,
  SDL2,
  libGL,
  xorg,
  kdePackages,
  qt6Packages ? kdePackages.qt6Packages,
  installShellFiles,
  copyDesktopItems,
  makeDesktopItem,
  graphical ? false,
  graphical-qt ? false,
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
  pname = if graphical then (if graphical-qt then "kge-qt" else "kge") else "kte";
  inherit version;

  src = lib.cleanSource ./.;

  nativeBuildInputs = [
    cmake
    installShellFiles
  ] ++ lib.optionals graphical [
    copyDesktopItems
  ] ++ lib.optionals graphical-qt [
    qt6Packages.wrapQtAppsHook
  ];

  buildInputs = [
    ncurses
  ] ++ lib.optionals graphical [
    SDL2
    libGL
    xorg.libX11
  ] ++ lib.optionals graphical-qt [
    kdePackages.qt6ct
    qt6Packages.qtbase
  ];

  cmakeFlags = [
    "-DBUILD_GUI=${if graphical then "ON" else "OFF"}"
    "-DKTE_USE_QT=${if graphical-qt then "ON" else "OFF"}"
    "-DCMAKE_BUILD_TYPE=Debug"
    "-DKTE_STATIC_LINK=OFF"
  ];

  desktopItems = lib.optionals graphical [
    (makeDesktopItem {
      name = "kge";
      desktopName = "kge";
      genericName = "Text Editor";
      comment = "kyle's graphical text editor";
      exec = if graphical-qt then "kge-qt %F" else "kge %F";
      icon = "kge";
      terminal = false;
      categories = [ "Utility" "TextEditor" "Development" ];
      mimeTypes = [
        "text/plain"
        "text/x-c"
        "text/x-c++"
        "text/x-python"
        "text/x-go"
        "text/x-rust"
        "application/json"
        "text/markdown"
        "text/x-shellscript"
      ];
    })
  ];

  installPhase = ''
    runHook preInstall

    mkdir -p $out/bin
    cp kte $out/bin/
    installManPage ../docs/kte.1

    ${lib.optionalString graphical ''
      ${if graphical-qt then ''
        cp kge $out/bin/kge-qt
      '' else ''
        cp kge $out/bin/kge
      ''}
      installManPage ../docs/kge.1

      mkdir -p $out/share/icons/hicolor/256x256/apps
      cp ../kge.png $out/share/icons/hicolor/256x256/apps/kge.png
    ''}

    runHook postInstall
  '';

  meta = {
    description = "kyle's text editor" + lib.optionalString graphical " (graphical)";
    platforms = lib.platforms.unix;
    mainProgram = if graphical then (if graphical-qt then "kge-qt" else "kge") else "kte";
  };
}
