# Developer controls

Developer remote control is an opt-in developer facility and is off by default.
Enable it in `switches.txt` with `remote token=<secret>`; both the `remote`
switch and a nonempty token are required. For BreadBox, the file belongs at
`sdmc:/3ds/BreadBox/switches.txt`. For compatibility with existing PSX3D
developer setups, BreadBox also reads `sdmc:/3ds/PSX3D/switches.txt` when its
own file is absent.

Remote control is intended for a trusted local area network. It does not use
TLS, so do not expose it to the public internet. The optional `update=<URL>`
command accepts a LAN URL only; public hosts and addresses are refused.
