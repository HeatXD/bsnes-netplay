auto NetplaySettings::create() -> void {
  setCollapsible();

  weyvelengthLabel.setText("Weyvelength").setFont(Font().setBold());

  hostLabel.setText("Server:");
  hostValue.setText(settings.weyvelength.host).setToolTip(
    "Address of the Weyvelength room server to connect to.\n"
    "Online Rooms connects here automatically when opened."
  ).onChange([&] {
    settings.weyvelength.host = hostValue.text().strip();
  });
  portLabel.setText("Port:");
  portValue.setText({settings.weyvelength.port}).setToolTip("Room server port (default 5555).")
  .onChange([&] {
    settings.weyvelength.port = portValue.text().strip().natural();
  });

  nicknameLabel.setText("Nickname:");
  nicknameValue.setText(settings.weyvelength.nickname).setToolTip(
    "Name other players see in the member list and chat."
  ).onChange([&] {
    settings.weyvelength.nickname = nicknameValue.text().strip();
  });

  gamesFolderLabel.setText("Games:");
  gamesFolderPath.setEditable(false).setText(settings.weyvelength.gamesFolder).setToolTip(
    "Folder scanned recursively for .sfc/.smc ROMs.\n"
    "The host picks from this list, and each player auto-loads their own copy by content hash."
  );
  gamesFolderAssign.setText("Assign ...").onActivate([&] {
    if(auto location = program.selectPath()) {
      settings.weyvelength.gamesFolder = location;
      gamesFolderPath.setText(location);
    }
  });
}
