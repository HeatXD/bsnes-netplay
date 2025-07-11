auto NetplayWindow::create() -> void {
    layout.setPadding(6_sx);

    portLabel.setText("Local Port:");
    portValue.onChange([&] { config.localPort = portValue.text().strip().integer();});

    rollbackframeLabel.setText({"Max Rollback: ", config.rollbackframes});
    rollbackframeValue.setPosition(config.rollbackframes);
    rollbackframeValue.setLength(11).onChange([&] { 
        config.rollbackframes = rollbackframeValue.position();
        rollbackframeLabel.setText({"Max Rollback: ", config.rollbackframes}); 
    });

    delayLabel.setText({"Delay: ", config.localDelay});
    delayValue.setPosition(config.localDelay);
    delayValue.setLength(16).onChange([&] { 
        config.localDelay = delayValue.position();
        delayLabel.setText({"Delay: ", config.localDelay}); 
    });

    playerLabel.setText("Local Controller:");
    p1Select.setText("P1").onActivate([&] { config.localPlayer = 0; });
    p2Select.setText("P2").onActivate([&] { config.localPlayer = 1; });
    specSelect.setText("None").onActivate([&] { config.localPlayer = 2; });

    remoteLabel.setText("Remote Player Address:");
    remoteAddressValue.onChange([&] { 
        config.remoteAddress = remoteAddressValue.text().strip();
        //print("Remote Address: ", remoteAddressValue.text(), "\n");
    });

    spectatorLabel.setText("Spectator Addresses:");
    spectatorValue.onChange([&] { 
        config.spectators = spectatorValue.text().strip().split(",");
       // print("Spectators: ", spectatorValue.text(), "\n");
    });

    if(config.localPlayer == 0) p1Select.setChecked();
    if(config.localPlayer == 1) p2Select.setChecked();
    if(config.localPlayer == 2) specSelect.setChecked();

    btnStart.setText("Start Session").setIcon(Icon::Media::Play).onActivate([&] {
        if(portValue.text().strip().size() == 0) {
            MessageDialog("Local port number is required")
            .setTitle("Netplay Setup Error")
            .error();
            return;
        }
        if(remoteAddressValue.text().strip().size() == 0) {
            MessageDialog("Remote player address is required")
            .setTitle("Netplay Setup Error")
            .error();
            return;
        }
        program.netplayStart(config.localPort, config.localPlayer, config.rollbackframes, config.localDelay, config.remoteAddress, config.spectators);
        doClose(); 
    });
    btnCancel.setText("Cancel").setIcon(Icon::Action::Quit).onActivate([&] { doClose(); });

    setTitle("Netplay Setup");
    setSize({450_sx, 300_sy});
    setResizable(false);
    setAlignment({0.5, 0.5});

    onClose([&] {
        setVisible(false);
    });
}

auto NetplayWindow::setVisible(bool visible) -> NetplayWindow& {
    if(visible) {
        Application::processEvents();
    }
    return Window::setVisible(visible), *this;
}

auto NetplayWindow::show() -> void {
    setVisible();
    setFocused();
}