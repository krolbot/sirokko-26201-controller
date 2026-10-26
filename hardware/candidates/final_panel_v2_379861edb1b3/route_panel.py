import pcbnew

PCB = "/projects/exports/sirokko_final_panel_candidate_v1/Sirokko26201_control_motor_candidate.kicad_pcb"
b = pcbnew.LoadBoard(PCB)

def v(x, y):
    return pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))

def route(net, layer, points, width=0.25):
    code = b.GetNetInfo().GetNetItem(net).GetNetCode()
    lid = b.GetLayerID(layer)
    for a, z in zip(points, points[1:]):
        t = pcbnew.PCB_TRACK(b)
        t.SetStart(v(*a))
        t.SetEnd(v(*z))
        t.SetWidth(pcbnew.FromMM(width))
        t.SetLayer(lid)
        t.SetNetCode(code)
        b.Add(t)

def via(net, x, y, size=0.8, drill=0.4):
    code = b.GetNetInfo().GetNetItem(net).GetNetCode()
    q = pcbnew.PCB_VIA(b)
    q.SetPosition(v(x, y))
    q.SetWidth(pcbnew.FromMM(size))
    q.SetDrill(pcbnew.FromMM(drill))
    q.SetLayerPair(b.GetLayerID("F.Cu"), b.GetLayerID("B.Cu"))
    q.SetNetCode(code)
    b.Add(q)

# Independent dry-contact pole of the reset switch.
route("/RESET_SAFE_NO", "B.Cu", [(87.46,165.54),(78,165.54),(70,164)], 0.30)
route("/RESET_SAFE_COM", "B.Cu", [(87.46,163),(76,163),(65,164)], 0.30)

# Start/stop and reset requests to the existing pull-up networks.
route("/RESET_REQUEST", "F.Cu", [(92.54,165.54),(98,160),(98,112),(135.16,112),(135.16,105)])
route("/START_REQUEST", "F.Cu", [(90,149),(95,149),(95,102.5),(155.16,102.5),(155.16,105)])

# Encoder A and B, including the new 10 nF filters.
route("/ENC_A", "F.Cu", [(87.5,135.5),(87.5,118),(107.1375,118),(107.1375,116)])
route("/ENC_A", "F.Cu", [(107.1375,116),(120,116),(120,113),(155.16,113),(155.16,110)])
route("/ENC_B", "F.Cu", [(92.5,135.5),(92.5,122),(111.1375,122),(111.1375,116)])
route("/ENC_B", "F.Cu", [(111.1375,116),(116,116),(116,114.5),(175.16,114.5),(175.16,110)])

# Encoder push switch and its RC filter; long run to J3 is moved to B.Cu.
route("/ENC_SW", "F.Cu", [(87.5,121),(80,121),(80,114),(100.9125,114),(100.9125,116)])
route("/ENC_SW", "F.Cu", [(100.9125,116),(103.1375,116),(106,116)])
via("/ENC_SW", 106,116)
route("/ENC_SW", "B.Cu", [(106,116),(76,116),(76,57.86),(83,57.86)])

# Pull-up supply and filter returns are tied into their copper zones with vias.
route("/+3V3", "F.Cu", [(99.0875,116),(97.5,116)])
via("/+3V3", 97.5,116)
for x0, xv, yv in [(104.8625,104.8625,113),(108.8625,108.8625,113),(112.8625,112.8625,112)]:
    route("/GND", "F.Cu", [(x0,116),(xv,yv)])
    via("/GND", xv,yv)

pcbnew.SaveBoard(PCB, b)
print("PANEL_ROUTES_ADDED")
