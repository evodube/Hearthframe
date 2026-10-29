# New frame set (from the Artisanix "ax paintings" meshes). Source mesh id -> (shape, style name).
# Style names come from looking at each frame's diffuse texture. Square "lc02" meshes reuse the lc01 textures.
SHAPES = [("Landscape", 52.0, 32.0), ("Square", 32.3, 32.0), ("Portrait", 32.3, 45.9)]
LC = ["Driftwood", "Black Marble", "Grey Marble", "Birch", "Pale Birch", "Moss Grey", "Weathered Oak", "Dark Oak", "Rough Pine", "Gnarled Elm"]
FRAMES = []
for i, n in enumerate(LC):
    FRAMES.append((f"lc01{i+1:02d}", "Landscape", "Rustic " + n))
for i, n in enumerate(["Blue, Copper Trim", "Blue, Gold Trim", "Oak, Blue Trim", "Painted Blue", "Terracotta", "Olive, Gold Trim", "Painted Taupe", "Teal, Copper Trim", "Green, Gold Trim", "Umber, Green Trim"]):
    FRAMES.append((f"mc01{i+1:02d}", "Landscape", "Painted " + n if not n.startswith("Painted") else n))
for i, n in enumerate(["Carved Ebony", "Carved Walnut Knotwork", "Carved Oak", "Mahogany", "Blackwood Knotwork", "Amber Burl", "Distressed Walnut", "Golden Teak", "Dark Walnut Knotwork"]):
    FRAMES.append((f"uc0{i//3+1}0{i%3+1}", "Landscape", "Noble " + n))
for i, n in enumerate(LC):
    FRAMES.append((f"lc02{i+1:02d}", "Square", "Rustic " + n))
for i, n in enumerate(["Dark Walnut", "Walnut", "Walnut, Blue Inlay", "Black, Gold Inlay", "Blackened Oak", "Honey Oak", "Golden Oak", "Cherry", "Rosewood", "Ebony"]):
    FRAMES.append((f"mc02{i+1:02d}", "Square", n))
for i, n in enumerate(["Olive Burl", "Dark Burl", "Burl, Pale Trim", "Pine, Blue Trim", "Pine", "Knotty Pine", "Red Cedar", "Smoked Oak", "Mahogany", "Black Walnut"]):
    FRAMES.append((f"mc03{i+1:02d}", "Portrait", n))

def file_stem(shape, style):
    return shape + "_" + "".join(ch for ch in style.title() if ch.isalnum())
