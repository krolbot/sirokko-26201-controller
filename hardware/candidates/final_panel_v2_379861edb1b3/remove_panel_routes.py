import pcbnew

PCB = "/projects/exports/sirokko_final_panel_candidate_v1/Sirokko26201_control_motor_candidate.kicad_pcb"
b = pcbnew.LoadBoard(PCB)

def key(a, z):
    p = (round(a[0], 4), round(a[1], 4))
    q = (round(z[0], 4), round(z[1], 4))
    return tuple(sorted((p, q)))

paths = [
("/RESET_SAFE_NO",[(87.46,165.54),(78,165.54),(70,164)]),
("/RESET_SAFE_COM",[(87.46,163),(76,163),(65,164)]),
("/RESET_REQUEST",[(92.54,165.54),(98,160),(98,112),(135.16,112),(135.16,105)]),
("/START_REQUEST",[(90,149),(95,149),(95,102.5),(155.16,102.5),(155.16,105)]),
("/ENC_A",[(87.5,135.5),(87.5,118),(107.1375,118),(107.1375,116)]),
("/ENC_A",[(107.1375,116),(120,116),(120,113),(155.16,113),(155.16,110)]),
("/ENC_B",[(92.5,135.5),(92.5,122),(111.1375,122),(111.1375,116)]),
("/ENC_B",[(111.1375,116),(116,116),(116,114.5),(175.16,114.5),(175.16,110)]),
("/ENC_SW",[(87.5,121),(80,121),(80,114),(100.9125,114),(100.9125,116)]),
("/ENC_SW",[(100.9125,116),(103.1375,116),(106,116)]),
("/ENC_SW",[(106,116),(76,116),(76,57.86),(83,57.86)]),
("/+3V3",[(99.0875,116),(97.5,116)]),
("/GND",[(104.8625,116),(104.8625,113)]),
("/GND",[(108.8625,116),(108.8625,113)]),
("/GND",[(112.8625,116),(112.8625,112)]),
]
segments = {(net,key(a,z)) for net,pts in paths for a,z in zip(pts,pts[1:])}
vias = {("/ENC_SW",106.0,116.0),("/+3V3",97.5,116.0),("/GND",104.8625,113.0),("/GND",108.8625,113.0),("/GND",112.8625,112.0)}
removed = 0
for item in list(b.GetTracks()):
    net = item.GetNetname()
    if isinstance(item, pcbnew.PCB_VIA):
        p = item.GetPosition()
        vk = (net,round(p.x/1e6,4),round(p.y/1e6,4))
        if vk in vias:
            b.Remove(item); removed += 1
    elif isinstance(item, pcbnew.PCB_TRACK):
        a=item.GetStart(); z=item.GetEnd()
        sk=(net,key((a.x/1e6,a.y/1e6),(z.x/1e6,z.y/1e6)))
        if sk in segments:
            b.Remove(item); removed += 1
pcbnew.SaveBoard(PCB,b)
print("REMOVED",removed,"EXPECTED",len(segments)+len(vias))
