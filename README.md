[![webrtsp-signaling-server](https://snapcraft.io/webrtsp-signaling-server/badge.svg)](https://snapcraft.io/webrtsp-signaling-server)

# WebRTSP Signaling Server

Enables remote access to WebRTSP agents located behind NAT.

*Note: If you don't want to set up your own server, I can grant you access to my test server.
Please contact me and share [your Agent ID](#how-to-find-out-id-of-specific-agent) and
I'll provide you with the URL to use in your agent's config file.*

## Online demos
* With [WebRTSP Camera](https://snapcraft.io/webrtsp-camera-streamer) as Agent: [signaling.webrtsp.org](https://signaling.webrtsp.org/view#~64vzy4n8r5g1ja8)
* With [WebRTSP HDMI ReStreamer](https://snapcraft.io/webrtsp-hdmi-restreamer) as Agent: [signaling.webrtsp.org](https://signaling.webrtsp.org/view#~64w3hrk82fcn2bg)

## How to install it as Snap package
`sudo snap install webrtsp-signaling-server --edge`

## How enable TLS with Let's Encrypt certificate (highly recommended)
`./enableTLS.sh admin@your.server.address:22 you@example.com`

## How to edit config file
`sudoedit /var/snap/webrtsp-signaling-server/common/signaling-server.conf`

## How to find out ID of specific Agent
* For [WebRTSP Camera](https://snapcraft.io/webrtsp-camera-streamer):
```
$ sudo cat /var/snap/webrtsp-camera-streamer/common/id
28813e16-d16a-48c2-a130-9f68068f15a0
```
* For [WebRTSP HDMI ReStreamer](https://snapcraft.io/webrtsp-hdmi-restreamer):
```
sudo cat /var/snap/webrtsp-hdmi-restreamer/common/id
79ee7063-5fc9-4dc6-be2b-556f31df3553
```

## How to register a new Agent for remote access
1. Generate new credentials for a specific Agent on the server:
```
$ sudo snap run webrtsp-signaling-server.SignalingServer -g 28813e16-d16a-48c2-a130-9f68068f15a0
Agent ID: ~64vehj3gw9fe7ar
Access token: 77TE1fshlzZcKJ7pU-ZppRFRxaC105R6_b6bZgyLSog
```

2. Add `signaling-server` to the Agent's config:
```
signaling-server: "webrtsps://~64vehj3gw9fe7ar:77TE1fshlzZcKJ7pU-ZppRFRxaC105R6_b6bZgyLSog@signaling.webrtsp.org"
```

3. Add users to the Agent's config:
```
users: (
  {
    login: "user"
    pass: "password"
  },
  {
    login: "another_user"
    pass: "password"
  }
)
```

4. Restart your Agent:
* [WebRTSP Camera](https://snapcraft.io/webrtsp-camera-streamer): `sudo snap restart webrtsp-camera-streamer`
* [WebRTSP HDMI ReStreamer](https://snapcraft.io/webrtsp-hdmi-restreamer): `sudo snap restart webrtsp-hdmi-restreamer`

5. Now you can access your Agent by entering the following URL in your browser:
```
https://your.server.address:5443/view#user@password/~64vehj3gw9fe7ar
```

## Troubleshooting
* To see application logs in realtime run: `sudo snap logs webrtsp-signaling-server -f`
