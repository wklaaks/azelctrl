#!/usr/bin/env python3
"""
Generate simplified world coastline/boundary data for embedding in AZELCtrl orbital map.

Uses world-atlas countries-10m TopoJSON (from topojson/world-atlas on npm).
Decodes TopoJSON arcs, simplifies with Douglas-Peucker, outputs embedded JS constant.

Target: ~500 KB output, recognizable country/island-level detail.
"""

import json
import math
import os
import sys

TOPJSON_FILE = os.path.join(os.path.dirname(__file__), "world-10m.json")
OUT_FILE = os.path.join(os.path.dirname(__file__), "world_coastline.js")
TARGET_SIZE = 500_000  # ~500 KB target


def decode_topojson(topology):
    """Decode TopoJSON arcs into list of ring coordinate lists [lon, lat]."""
    arcs_map = topology.get("arcs", [])

    def transform_coords(coords):
        """Apply TopoJSON transform (delta decoding)."""
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

    # Decode all arcs
    decoded_arcs = []
    for arc in arcs_map:
        decoded_arcs.append(transform_coords(arc))

    return decoded_arcs


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


def build_ring(arc_indices, decoded_arcs):
    """Build a coordinate ring from arc index references (handles negative = reversed)."""
    coords = []
    for idx in arc_indices:
        if idx >= 0:
            coords.extend(decoded_arcs[idx])
        else:
            # Negative index = reversed arc
            reversed_arc = list(decoded_arcs[-(idx + 1)])
            reversed_arc.reverse()
            coords.extend(reversed_arc)

    # Remove duplicate first/last point if present
    if len(coords) > 1:
        first = coords[0]
        last = coords[-1]
        if abs(first[0] - last[0]) < 0.0001 and abs(first[1] - last[1]) < 0.0001:
            coords = coords[:-1]

    # Convert [lon, lat] to [lat, lon]
    return [[c[1], c[0]] for c in coords]


def douglas_peucker(points, tolerance):
    """Simplify a polyline using Ramer-Douglas-Peucker algorithm."""
    if len(points) <= 2:
        return points

    # Find the point with the maximum distance from the line between first and last
    start = points[0]
    end = points[-1]
    max_dist = 0
    max_idx = 0

    dx = end[0] - start[0]
    dy = end[1] - start[1]
    line_len_sq = dx * dx + dy * dy

    if line_len_sq < 1e-10:
        line_len_sq = 1

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


def estimate_js_size(rings):
    """Estimate the JavaScript output size in bytes."""
    total_points = sum(len(r) for r in rings)
    return total_points * 18 + len(rings) * 20 + 200


def main():
    # Allow command-line override of tolerance
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

    topology = topo.get("objects", {}).get("countries", {}).get("arcs_reference", "countries")
    # The TopoJSON structure: objects.countries is a GeometryCollection
    countries_obj = topo["objects"]["countries"]
    topo_obj = topo

    # Decode arcs
    print("Decoding TopoJSON arcs...")
    decoded_arcs = decode_topojson(topo_obj)
    print(f"Decoded {len(decoded_arcs)} arcs")

    # Extract geometries
    print("Extracting country boundaries...")
    all_rings = []
    geometries = countries_obj.get("geometries", [])
    for geom in geometries:
        props = geom.get("properties", {})
        name = props.get("name", "unknown")
        rings = get_geometry_coords(geom, decoded_arcs)
        all_rings.extend(rings)

    print(f"Extracted {len(all_rings)} rings from {len(geometries)} countries")

    # Filter out tiny rings (lakes, islands < 0.5° area)
    def ring_area(ring):
        """Rough area estimate using bounding box."""
        if len(ring) < 3:
            return 0
        lats = [p[0] for p in ring]
        lons = [p[1] for p in ring]
        return (max(lats) - min(lats)) * (max(lons) - min(lons))

    print("Filtering tiny rings...")
    filtered_rings = [r for r in all_rings if ring_area(r) > 0.25]  # 0.5° x 0.5° minimum
    print(f"Filtered to {len(filtered_rings)} rings")

    # If forced tolerance, use it directly
    if force_tolerance is not None:
        best_tolerance = force_tolerance
        best_simplified = []
        for ring in filtered_rings:
            s = douglas_peucker(ring, force_tolerance)
            if len(s) >= 2:
                best_simplified.append(s)
        print(f"\nUsing forced tolerance: {force_tolerance}°")
    else:
        # Try different tolerances to hit target size
        print("\nSimplification test:")
        best_tolerance = None
        best_simplified = None

        for tolerance in [0.05, 0.1, 0.15, 0.2, 0.25, 0.3, 0.4, 0.5, 0.6, 0.8, 1.0]:
            simplified = []
            for ring in filtered_rings:
                s = douglas_peucker(ring, tolerance)
                if len(s) >= 2:
                    simplified.append(s)

            size = estimate_js_size(simplified)
            total_points = sum(len(r) for r in simplified)
            print(f"  Tolerance {tolerance:5.2f}°: {len(simplified):5d} rings, {total_points:7d} points, ~{size/1024:6.0f} KB")

            best_tolerance = tolerance
            best_simplified = simplified

            if size <= TARGET_SIZE:
                break

        if best_simplified is None:
            best_simplified = simplified
            best_tolerance = 0.3

        print(f"\nSelected tolerance: {best_tolerance}°")

    total_points = sum(len(r) for r in best_simplified)
    print(f"Total rings: {len(best_simplified)}, points: {total_points}")

    # Write JavaScript output
    print(f"Writing {OUT_FILE}...")
    with open(OUT_FILE, "w") as f:
        f.write("// World coastline data — Natural Earth 1:10m via topojson/world-atlas\n")
        f.write(f"// Simplified with Douglas-Peucker (tolerance={best_tolerance}°)\n")
        f.write(f"// {len(best_simplified)} rings, {total_points} total points\n\n")
        f.write("const WORLD_OUTLINES = [\n")

        for ring in best_simplified:
            coords = []
            for lat, lon in ring:
                lat_r = round(lat, 1)
                lon_r = round(lon, 1)
                coords.append(f"[{lat_r},{lon_r}]")
            f.write(f"  [{', '.join(coords)}],\n")

        f.write("];\n")

    # Check final size
    final_size = os.path.getsize(OUT_FILE)
    print(f"\nWritten: {OUT_FILE}")
    print(f"Size: {final_size:,} bytes ({final_size/1024:.0f} KB)")
    print(f"Rings: {len(best_simplified)}, Points: {total_points}")

    if final_size > TARGET_SIZE * 1.3:
        print(f"\nWarning: Output exceeds target size ({TARGET_SIZE/1024:.0f} KB)")
        print("Consider increasing tolerance.")


if __name__ == "__main__":
    main()
