import UIKit
import AVFoundation

/**
 * Main view controller - replaces Win32 panel UI.
 */
class ViewController: UIViewController {

    // MARK: - UI
    private let statusLabel = UILabel()
    private let serverField = UITextField()
    private let roomField = UITextField()
    private let nickField = UITextField()
    private let passwordField = UITextField()
    private let joinButton = UIButton(type: .system)
    private let leaveButton = UIButton(type: .system)
    private let memberTableView = UITableView()
    private let joinPanel = UIView()
    private let roomPanel = UIView()

    // MARK: - State
    private var members: [QminiPeer] = []
    private var audioEngine: AVAudioEngine?
    private var playerNode: AVAudioPlayerNode?

    override func viewDidLoad() {
        super.viewDidLoad()
        setupUI()
        loadConfig()
    }

    private func setupUI() {
        view.backgroundColor = UIColor(red: 0.1, green: 0.1, blue: 0.18, alpha: 1.0)

        // Status
        statusLabel.text = "Disconnected"
        statusLabel.textColor = UIColor(red: 0.91, green: 0.27, blue: 0.38, alpha: 1.0)
        statusLabel.font = .systemFont(ofSize: 14)

        // Join panel fields
        let fields = [serverField, roomField, nickField, passwordField]
        let hints = ["Server (e.g. 192.168.1.100)", "Room Name", "Nickname", "Password (optional)"]
        for (field, hint) in zip(fields, hints) {
            field.placeholder = hint
            field.backgroundColor = UIColor(red: 0.086, green: 0.129, blue: 0.243, alpha: 1.0)
            field.textColor = .white
            field.borderStyle = .roundedRect
            field.attributedPlaceholder = NSAttributedString(
                string: hint,
                attributes: [.foregroundColor: UIColor.gray]
            )
        }
        passwordField.isSecureTextEntry = true

        // Join button
        joinButton.setTitle("Join Room", for: .normal)
        joinButton.backgroundColor = UIColor(red: 0.91, green: 0.27, blue: 0.38, alpha: 1.0)
        joinButton.setTitleColor(.white, for: .normal)
        joinButton.layer.cornerRadius = 8
        joinButton.addTarget(self, action: #selector(onJoin), for: .touchUpInside)

        // Leave button
        leaveButton.setTitle("Leave Room", for: .normal)
        leaveButton.backgroundColor = UIColor(red: 0.325, green: 0.204, blue: 0.514, alpha: 1.0)
        leaveButton.setTitleColor(.white, for: .normal)
        leaveButton.layer.cornerRadius = 8
        leaveButton.addTarget(self, action: #selector(onLeave), for: .touchUpInside)

        // Member table
        memberTableView.backgroundColor = UIColor(red: 0.086, green: 0.129, blue: 0.243, alpha: 1.0)
        memberTableView.delegate = self
        memberTableView.dataSource = self
        memberTableView.register(UITableViewCell.self, forCellReuseIdentifier: "cell")

        // Layout
        layoutPanels()
    }

    private func layoutPanels() {
        // Add subviews and use Auto Layout
        [statusLabel, joinPanel, roomPanel].forEach {
            $0.translatesAutoresizingMaskIntoConstraints = false
            view.addSubview($0)
        }

        // Join panel contents
        let stackView = UIStackView(arrangedSubviews: [serverField, roomField, nickField, passwordField, joinButton])
        stackView.axis = .vertical
        stackView.spacing = 10
        stackView.translatesAutoresizingMaskIntoConstraints = false
        joinPanel.addSubview(stackView)
        joinPanel.translatesAutoresizingMaskIntoConstraints = false

        // Room panel contents
        let roomStack = UIStackView(arrangedSubviews: [memberTableView, leaveButton])
        roomStack.axis = .vertical
        roomStack.spacing = 12
        roomStack.translatesAutoresizingMaskIntoConstraints = false
        roomPanel.addSubview(roomStack)
        roomPanel.translatesAutoresizingMaskIntoConstraints = false
        roomPanel.isHidden = true

        NSLayoutConstraint.activate([
            statusLabel.topAnchor.constraint(equalTo: view.safeAreaLayoutGuide.topAnchor, constant: 16),
            statusLabel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 16),

            joinPanel.topAnchor.constraint(equalTo: statusLabel.bottomAnchor, constant: 16),
            joinPanel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 16),
            joinPanel.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -16),

            stackView.topAnchor.constraint(equalTo: joinPanel.topAnchor),
            stackView.leadingAnchor.constraint(equalTo: joinPanel.leadingAnchor),
            stackView.trailingAnchor.constraint(equalTo: joinPanel.trailingAnchor),
            stackView.bottomAnchor.constraint(equalTo: joinPanel.bottomAnchor),

            roomPanel.topAnchor.constraint(equalTo: statusLabel.bottomAnchor, constant: 16),
            roomPanel.leadingAnchor.constraint(equalTo: view.leadingAnchor, constant: 16),
            roomPanel.trailingAnchor.constraint(equalTo: view.trailingAnchor, constant: -16),
            roomPanel.bottomAnchor.constraint(equalTo: view.safeAreaLayoutGuide.bottomAnchor, constant: -16),

            roomStack.topAnchor.constraint(equalTo: roomPanel.topAnchor),
            roomStack.leadingAnchor.constraint(equalTo: roomPanel.leadingAnchor),
            roomStack.trailingAnchor.constraint(equalTo: roomPanel.trailingAnchor),
            roomStack.bottomAnchor.constraint(equalTo: roomPanel.bottomAnchor),

            serverField.heightAnchor.constraint(equalToConstant: 44),
            roomField.heightAnchor.constraint(equalToConstant: 44),
            nickField.heightAnchor.constraint(equalToConstant: 44),
            passwordField.heightAnchor.constraint(equalToConstant: 44),
            joinButton.heightAnchor.constraint(equalToConstant: 44),
            leaveButton.heightAnchor.constraint(equalToConstant: 44),
        ])
    }

    @objc private func onJoin() {
        guard let server = serverField.text, !server.isEmpty,
              let room = roomField.text, !room.isEmpty,
              let nick = nickField.text, !nick.isEmpty else {
            return
        }
        saveConfig(server: server, room: room, nick: nick)
        statusLabel.text = "Connecting..."

        // TODO: Initialize audio engine + network
        showRoomPanel(true)
    }

    @objc private func onLeave() {
        // TODO: Disconnect
        members.removeAll()
        memberTableView.reloadData()
        statusLabel.text = "Disconnected"
        showRoomPanel(false)
    }

    private func showRoomPanel(_ inRoom: Bool) {
        joinPanel.isHidden = inRoom
        roomPanel.isHidden = !inRoom
    }

    // MARK: - Config
    private func saveConfig(server: String, room: String, nick: String) {
        UserDefaults.standard.set(server, forKey: "qmini_server")
        UserDefaults.standard.set(room, forKey: "qmini_room")
        UserDefaults.standard.set(nick, forKey: "qmini_nick")
    }

    private func loadConfig() {
        serverField.text = UserDefaults.standard.string(forKey: "qmini_server") ?? ""
        roomField.text = UserDefaults.standard.string(forKey: "qmini_room") ?? ""
        nickField.text = UserDefaults.standard.string(forKey: "qmini_nick") ?? ""
    }
}

// MARK: - UITableViewDataSource
extension ViewController: UITableViewDataSource, UITableViewDelegate {
    func tableView(_ tableView: UITableView, numberOfRowsInSection section: Int) -> Int {
        return members.count
    }

    func tableView(_ tableView: UITableView, cellForRowAt indexPath: IndexPath) -> UITableViewCell {
        let cell = tableView.dequeueReusableCell(withIdentifier: "cell", for: indexPath)
        let peer = members[indexPath.row]
        cell.textLabel?.text = "\(peer.nickname) (\(peer.id))"
        cell.textLabel?.textColor = .white
        cell.backgroundColor = .clear
        return cell
    }
}
