{
  description = "Local Call - LAN chat, file sharing and local calls";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      packages = forAllSystems (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "localcall";
          version = "2.0.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            qt6.wrapQtAppsHook
          ];

          buildInputs = with pkgs; [
            qt6.qtbase
            qt6.qtmultimedia
            qt6.qtwayland
            opencv
            nlohmann_json
            openssl
            opus
            openh264
            libyuv
            libdatachannel
          ];

          cmakeFlags = [
            "-DLOCALCALL_WITH_WEBRTC=AUTO"
            "-DLOCALCALL_WITH_OPENCV=AUTO"
            "-DLOCALCALL_WITH_MULTIMEDIA=ON"
          ];
        };
      });

      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          nativeBuildInputs = with pkgs; [ cmake ninja pkg-config qt6.wrapQtAppsHook ];
          buildInputs = with pkgs; [ qt6.qtbase qt6.qtmultimedia qt6.qtwayland opencv nlohmann_json openssl opus openh264 libyuv libdatachannel ];
        };
      });
    };
}
