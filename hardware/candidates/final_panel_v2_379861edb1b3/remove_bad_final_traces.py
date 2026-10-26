import pcbnew
PCB="/projects/exports/sirokko_final_panel_candidate_v1/Sirokko26201_control_motor_candidate.kicad_pcb"
b=pcbnew.LoadBoard(PCB)
def pxy(p): return (round(p.x/1e6,4),round(p.y/1e6,4))
def k(a,z): return tuple(sorted((a,z)))
target={k((87.46,165.54),(70.0,164.0)),k((87.46,163.0),(65.0,164.0)),k((100.9125,116.0),(103.1375,116.0))}
n=0
for t in list(b.GetTracks()):
    if isinstance(t,pcbnew.PCB_TRACK) and not isinstance(t,pcbnew.PCB_VIA) and k(pxy(t.GetStart()),pxy(t.GetEnd())) in target:
        b.Remove(t); n+=1
pcbnew.SaveBoard(PCB,b)
print("REMOVED",n)
