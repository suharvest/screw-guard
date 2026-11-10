import sys
import json
import jsonschema

PROFILE_SCHEMA_DRAFT07 = {
    "$schema": "http://json-schema.org/draft-07/schema#",
    "title": "ScrewGuardCalibExportDoc",
    "type": "object",
    "required": ["format", "exported_at", "profile", "confidence_threshold", "max_blocks"],
    "properties": {
        "format": { "type": "string", "enum": ["screw-guard-calib/1"] },
        "exported_at": { "type": "string" },
        "confidence_threshold": { "type": "number", "minimum": 0.50, "maximum": 0.95 },
        "max_blocks": { "type": "integer", "minimum": 1, "maximum": 4 },
        "profile": {
            "type": "object",
            "required": [
                "revision", "part_name", "expected_blocks", "expected_screws_per_block",
                "canonical_size", "min_block_area", "anchor", "screw_rois"
            ],
            "properties": {
                "revision": { "type": "integer", "minimum": 1 },
                "part_name": { "type": "string", "minLength": 1, "maxLength": 32 },
                "expected_blocks": { "type": "integer", "minimum": 1, "maximum": 4 },
                "expected_screws_per_block": { "type": "integer", "minimum": 1, "maximum": 32 },
                "canonical_size": {
                    "type": "array",
                    "items": { "type": "integer", "minimum": 30, "maximum": 1280 },
                    "minItems": 2,
                    "maxItems": 2
                },
                "min_block_area": { "type": "number", "exclusiveMinimum": 0 },
                "anchor": {
                    "type": "object",
                    "required": ["norm_x", "norm_y", "norm_w", "norm_h"],
                    "properties": {
                        "norm_x": { "type": "number", "minimum": 0, "maximum": 1 },
                        "norm_y": { "type": "number", "minimum": 0, "maximum": 1 },
                        "norm_w": { "type": "number", "exclusiveMinimum": 0, "maximum": 1 },
                        "norm_h": { "type": "number", "exclusiveMinimum": 0, "maximum": 1 },
                        "ref_edge_mean": { "type": ["number", "null"] }
                    },
                    "additionalProperties": False
                },
                "screw_rois": {
                    "type": "array",
                    "minItems": 1,
                    "maxItems": 32,
                    "items": {
                        "type": "object",
                        "required": ["id", "name", "norm_x", "norm_y", "norm_r"],
                        "properties": {
                            "id": { "type": "integer", "minimum": 1 },
                            "name": { "type": "string" },
                            "norm_x": { "type": "number", "minimum": 0, "maximum": 1 },
                            "norm_y": { "type": "number", "minimum": 0, "maximum": 1 },
                            "norm_r": { "type": "number", "exclusiveMinimum": 0, "maximum": 0.5 }
                        },
                        "additionalProperties": False
                    }
                }
            },
            "additionalProperties": True
        }
    }
}

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 validate_schema.py <path_to_json>")
        sys.exit(1)
    file_path = sys.argv[1]
    with open(file_path, "r", encoding="utf-8") as f:
        data = json.load(f)
    
    jsonschema.validate(instance=data, schema=PROFILE_SCHEMA_DRAFT07)
    profile = data["profile"]
    if profile["expected_screws_per_block"] != len(profile["screw_rois"]):
        raise jsonschema.ValidationError(
            "expected_screws_per_block must equal len(profile.screw_rois)"
        )
    if profile["expected_blocks"] > data["max_blocks"]:
        raise jsonschema.ValidationError(
            "profile.expected_blocks must be <= max_blocks"
        )
    print(f"JSON Schema Draft-07 Validation PASSED for {file_path}")

if __name__ == "__main__":
    main()
