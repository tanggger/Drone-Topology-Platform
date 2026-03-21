import xml.etree.ElementTree as ET
import sys

def inspect_osm(osm_file):
    print(f"Inspecting {osm_file}...")
    try:
        tree = ET.parse(osm_file)
        root = tree.getroot()
    except Exception as e:
        print(f"Error parsing OSM file: {e}")
        return

    count = 0
    for elem in root.findall('way') + root.findall('relation'):
        tags = {t.attrib['k']: t.attrib['v'] for t in elem.findall('tag')}
        if 'building' in tags and tags['building'] != 'no':
            print(f"\nBuilding #{count+1} (Type: {elem.tag}, ID: {elem.attrib.get('id')})")
            print(f"  Tags: {tags}")
            
            # Check for height related tags specifically
            height_tags = {k: v for k, v in tags.items() if 'height' in k or 'levels' in k}
            if height_tags:
                print(f"  -> Found Height/Levels info: {height_tags}")
            else:
                print("  -> NO Height/Levels info found (Defaulting to ~10m)")
                
            count += 1
            if count >= 10:
                break

if __name__ == "__main__":
    osm_path = "data_map/osm/map.osm"
    if len(sys.argv) > 1:
        osm_path = sys.argv[1]
    inspect_osm(osm_path)
