#!/usr/bin/env python3
"""
Generate country-boundary data for AZELCtrl orbital map, preserving per-country
polygon groupings so the renderer can fill each country with a distinct color.

Uses world-atlas countries-10m TopoJSON (from topojson/world-atlas on npm).
Outputs a JS constant: WORLD_COUNTRIES = [...], where each element is:
  { id: "004", name: "Afghanistan", rings: [[[lat,lon],...], ...] }

Target: ~600 KB unzipped (~130 KB gzipped).
"""

import json
import math
import os
import sys

TOPJSON_FILE = os.path.join(os.path.dirname(__file__), "world-10m.json")
OUT_FILE = os.path.join(os.path.dirname(__file__), "world_countries.js")
TARGET_SIZE = 600_000  # ~600 KB


def decode_topojson(topology):
    """Decode TopoJSON arcs into list of coordinate lists [lon, lat]."""
    def transform_coords(coords):
        transform = topology.get("transform", {})
        scale = transform.get("scale", [1, 1])
        translate = transform.get("translate", [0, 0])
        x, y = 0, 0
        result = []
        for dx, dy in coords:
            x += dx
            y += dy
            result.append([x * scale[0] + translate[0], y * scale[1] + translate[1]])
        return result

    decoded_arcs = []
    for arc in topology.get("arcs", []):
        decoded_arcs.append(transform_coords(arc))
    return decoded_arcs


def build_ring(arc_indices, decoded_arcs):
    """Build a coordinate ring from arc index references (negative = reversed)."""
    coords = []
    for idx in arc_indices:
        if idx >= 0:
            coords.extend(decoded_arcs[idx])
        else:
            reversed_arc = list(decoded_arcs[-(idx + 1)])
            reversed_arc.reverse()
            coords.extend(reversed_arc)

    # Remove duplicate first/last point
    if len(coords) > 1:
        first = coords[0]
        last = coords[-1]
        if abs(first[0] - last[0]) < 0.0001 and abs(first[1] - last[1]) < 0.0001:
            coords = coords[:-1]

    # Convert [lon, lat] to [lat, lon]
    return [[c[1], c[0]] for c in coords]


def get_geometry_coords(geom, decoded_arcs):
    """Extract coordinate rings from a TopoJSON geometry."""
    geom_type = geom.get("type", "")
    rings = []
    if geom_type == "Polygon":
        for arc_ring in geom["arcs"]:
            coords = build_ring(arc_ring, decoded_arcs)
            if coords:
                rings.append(coords)
    elif geom_type == "MultiPolygon":
        for polygon_arcs in geom["arcs"]:
            for arc_ring in polygon_arcs:
                coords = build_ring(arc_ring, decoded_arcs)
                if coords:
                    rings.append(coords)
    return rings


def douglas_peucker(points, tolerance):
    """Simplify a polyline using Ramer-Douglas-Peucker."""
    if len(points) <= 2:
        return points
    start = points[0]
    end = points[-1]
    dx = end[0] - start[0]
    dy = end[1] - start[1]
    line_len_sq = dx * dx + dy * dy
    if line_len_sq < 1e-10:
        line_len_sq = 1
    max_dist = 0
    max_idx = 0
    for i in range(1, len(points) - 1):
        t = ((points[i][0] - start[0]) * dx + (points[i][1] - start[1]) * dy) / line_len_sq
        t = max(0, min(1, t))
        proj_x = start[0] + t * dx
        proj_y = start[1] + t * dy
        dist = math.sqrt((points[i][0] - proj_x) ** 2 + (points[i][1] - proj_y) ** 2)
        if dist > max_dist:
            max_dist = dist
            max_idx = i
    if max_dist > tolerance:
        left = douglas_peucker(points[:max_idx + 1], tolerance)
        right = douglas_peucker(points[max_idx:], tolerance)
        return left[:-1] + right
    else:
        return [start, end]


def ring_area(ring):
    """Rough area estimate using bounding box."""
    if len(ring) < 3:
        return 0
    lats = [p[0] for p in ring]
    lons = [p[1] for p in ring]
    return (max(lats) - min(lats)) * (max(lons) - min(lons))


def main():
    force_tolerance = None
    if len(sys.argv) > 1:
        force_tolerance = float(sys.argv[1])

    if not os.path.exists(TOPJSON_FILE):
        print(f"Error: {TOPJSON_FILE} not found. Download with:")
        print(f'  curl -sL "https://unpkg.com/world-atlas@2/countries-10m.json" -o {TOPJSON_FILE}')
        sys.exit(1)

    print(f"Loading {TOPJSON_FILE} ({os.path.getsize(TOPJSON_FILE)/1024:.0f} KB)...")
    with open(TOPJSON_FILE) as f:
        topo = json.load(f)

    countries_obj = topo["objects"]["countries"]
    topo_obj = topo

    print("Decoding TopoJSON arcs...")
    decoded_arcs = decode_topojson(topo_obj)
    print(f"Decoded {len(decoded_arcs)} arcs")

    print("Extracting country polygons...")
    countries = []
    geometries = countries_obj.get("geometries", [])
    for geom in geometries:
        props = geom.get("properties", {})
        cid = str(geom.get("id", ""))
        name = props.get("name", "unknown")
        rings = get_geometry_coords(geom, decoded_arcs)
        if rings:
            countries.append({"id": cid, "name": name, "rings": rings})

    print(f"Extracted {len(countries)} countries")

    # Filter tiny rings from each country (lakes, tiny islands < 0.5° area)
    min_area = 0.25
    filtered_countries = []
    total_rings_before = 0
    total_rings_after = 0
    for c in countries:
        kept = [r for r in c["rings"] if ring_area(r) > min_area]
        total_rings_before += len(c["rings"])
        total_rings_after += len(kept)
        if kept:
            filtered_countries.append({"id": c["id"], "name": c["name"], "rings": kept})

    print(f"Rings: {total_rings_before} -> {total_rings_after} after filtering")

    # Simplify rings
    tolerance = force_tolerance if force_tolerance is not None else 0.05
    print(f"Simplifying with tolerance={tolerance}°...")
    simplified_countries = []
    total_points = 0
    for c in filtered_countries:
        new_rings = []
        for ring in c["rings"]:
            s = douglas_peucker(ring, tolerance)
            if len(s) >= 2:
                new_rings.append(s)
                total_points += len(s)
        simplified_countries.append({"id": c["id"], "name": c["name"], "rings": new_rings})

    # Count total rings after simplification
    total_rings = sum(len(c["rings"]) for c in simplified_countries)

    print(f"Total countries: {len(simplified_countries)}, rings: {total_rings}, points: {total_points}")

    # Write JavaScript output
    print(f"Writing {OUT_FILE}...")
    with open(OUT_FILE, "w") as f:
        f.write("// World country boundary data — Natural Earth 1:10m via topojson/world-atlas\n")
        f.write(f"// Simplified with Douglas-Peucker (tolerance={tolerance}°)\n")
        f.write(f"// {len(simplified_countries)} countries, {total_rings} rings, {total_points} points\n\n")
        f.write("const WORLD_COUNTRIES = [\n")

        for c in simplified_countries:
            f.write(f"  {{id:{json.dumps(c['id'])}, n:{json.dumps(c['name'])}, r:[\n")
            for ring in c["rings"]:
                coords = []
                for lat, lon in ring:
                    lat_r = round(lat, 1)
                    lon_r = round(lon, 1)
                    coords.append(f"[{lat_r},{lon_r}]")
                f.write(f"    [{', '.join(coords)}],\n")
            f.write("  ]},\n")

        f.write("];\n")

    # Write gzipped version
    print("Writing gzipped version...")
    import gzip
    gz_path = OUT_FILE + ".gz"
    with open(OUT_FILE, "rb") as raw:
        with gzip.open(gz_path, "wb", compresslevel=9) as gz:
            gz.write(raw.read())

    final_size = os.path.getsize(OUT_FILE)
    gz_size = os.path.getsize(gz_path)
    print(f"\nWritten: {OUT_FILE}")
    print(f"Raw size: {final_size:,} bytes ({final_size/1024:.0f} KB)")
    print(f"Gzip size: {gz_size:,} bytes ({gz_size/1024:.0f} KB)")

    if final_size > TARGET_SIZE * 1.3:
        print(f"\nWarning: Output exceeds target size ({TARGET_SIZE/1024:.0f} KB)")
        print("Consider increasing tolerance.")


if __name__ == "__main__":
    main()