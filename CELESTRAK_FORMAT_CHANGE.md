# CelesTrak GP Data Format Change

## The Problem

The traditional TLE (Two-Line Element) format has a **5-digit catalog number limitation**. This means satellites with NORAD catalog IDs >= 100000 cannot be represented in standard TLE format.

From CelesTrak's website:
> "All newly cataloged objects will have 6-digit catalog numbers of 100000+ and GP data will not be available for them using the TLE format."

Example:
- **ISS (25544)**: Works in TLE format ✓
- **SARAMAGO (100000)**: Returns "No GP data found" in TLE format ✗

## The Solution: CSV Format

CelesTrak provides a **CSV format** that contains the same Keplerian orbital elements but without the 5-digit limitation:

```
OBJECT_NAME,OBJECT_ID,EPOCH,MEAN_MOTION,ECCENTRICITY,INCLINATION,RA_OF_ASC_NODE,ARG_OF_PERICENTER,MEAN_ANOMALY,EPHEMERIS_TYPE,CLASSIFICATION_TYPE,NORAD_CAT_ID,ELEMENT_SET_NO,REV_AT_EPOCH,BSTAR,MEAN_MOTION_DOT,MEAN_MOTION_DDOT
SARAMAGO,2026-067CY,2026-07-20T04:06:41.059584,15.20516105,.00057357,97.4597,159.3072,245.7389,114.3247,0,U,100000,999,1671,.22413038E-3,.4833E-4,0
```

## API Endpoints

- **TLE format**: `https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=tle`
- **CSV format**: `https://celestrak.org/NORAD/elements/gp.php?CATNR=100000&FORMAT=csv`

## Implementation Strategy

1. **Fetch TLE first** - For backwards compatibility with satellites < 100000
2. **Fall back to CSV** - If TLE returns "No GP data found" or fails
3. **Store CSV data** - Keep the raw orbital elements in the satellite record
4. **Construct satrec from CSV** - When propagating, build the satrec object directly from CSV orbital elements instead of parsing TLE lines

## CSV to satrec Conversion

The CSV fields map directly to satellite.js satrec constructor parameters:

| CSV Field | satrec Parameter | Notes |
|-----------|------------------|-------|
| EPOCH | epoch | Convert ISO 8601 to Modified Julian Date |
| ECCENTRICITY | eccentricity | Direct |
| INCLINATION | inclination | Degrees → radians |
| RA_OF_ASC_NODE | nodeOmega | Degrees → radians |
| ARG_OF_PERICENTER | argPerigee | Degrees → radians |
| MEAN_ANOMALY | meanAnomaly | Degrees → radians |
| MEAN_MOTION | meanMotion | Revs/day → rad/min |
| BSTAR | bstar | Direct |
| MEAN_MOTION_DOT | nDot | Direct |
| MEAN_MOTION_DDOT | nDotDot | Direct |

## Changes Made

- Modified `fetchTleForNORAD()` to try TLE first, then CSV fallback
- Added `satrecFromSat()` helper to create satrec from either TLE or CSV data
- Updated `startTracking()` and `computeAndSend()` to use `satrecFromSat()`
- Satellite records now optionally store `csvData` object for satellites >= 100000
