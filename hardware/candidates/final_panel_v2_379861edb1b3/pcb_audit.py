import pcbnew, sys
p=sys.argv[1]
b=pcbnew.LoadBoard(p)
print('BOARD',*[v/1e6 for v in (b.GetBoardEdgesBoundingBox().GetX(),b.GetBoardEdgesBoundingBox().GetY(),b.GetBoardEdgesBoundingBox().GetWidth(),b.GetBoardEdgesBoundingBox().GetHeight())])
for fp in sorted(b.GetFootprints(), key=lambda x:x.GetReference()):
    bb=fp.GetBoundingBox(False,False)
    print(fp.GetReference(),fp.GetValue(),fp.GetLayerName(),*[round(v/1e6,3) for v in (bb.GetX(),bb.GetY(),bb.GetWidth(),bb.GetHeight())])
