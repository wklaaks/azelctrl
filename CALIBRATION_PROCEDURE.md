# Yaesu Rotator Controller - Potentiometer Calibration Procedure

## Pre-Calibration Setup (Manual)

**IMPORTANT: This step MUST be performed before connecting the ESP unit to the Control Box.**

### Step 1: Adjust Azimuth Potentiometer

1. Using a multimeter, measure the voltage output on **Pin 6** (1 o'clock position) of the DIN connector.
2. Move the Azimuth control to **180°** (maximum meter deflection).
3. Adjust the Azimuth potentiometer until the multimeter reads **exactly 3.2V**.
   - **To reduce voltage:** Turn the pot **Clockwise (CW)**
   - **To increase voltage:** Turn the pot **Counter-Clockwise (CCW)**

### Step 2: Adjust Elevation Potentiometer

1. Using a multimeter, measure the voltage output on **Pin 1** (3 o'clock position) of the DIN connector.
2. Move the Elevation control to **180°** (maximum meter deflection).
3. Adjust the Elevation potentiometer until the multimeter reads **exactly 3.2V**.
   - **To reduce voltage:** Turn the pot **Clockwise (CW)**
   - **To increase voltage:** Turn the pot **Counter-Clockwise (CCW)**

### DIN Connector Pin Reference

| Pin | Position | Function |
|-----|----------|----------|
| Pin 6 | 1 o'clock | Azimuth voltage output |
| Pin 1 | 3 o'clock | Elevation voltage output |

---

## Calibration Procedure (Automated)

Once the ESP unit is connected to the Control Box via the DIN connector, follow this automated calibration sequence.

### Entering Calibration Mode

1. Navigate to **AZ POT** or **EL POT** in the menu.
2. Press **Btn1** (short press) to start calibration.
3. The OLED displays: `CAL: AZ POT` or `CAL: EL POT`

### Phase 1: Initial Detection

**Goal:** Determine if the rotator is at the minimum voltage position.

1. **Take initial voltage measurement** (V1)
   - Reads the current voltage from the ADS1115

2. **Command movement for 5 seconds**
   - **AZ calibration:** Command **Clockwise (CW)** movement
   - **EL calibration:** Command **UP** movement

3. **Stop movement and take second measurement** (V2)

4. **Compare voltages**
   - Calculate: `ΔV = V2 - V1`
   - If `ΔV > 100mV` (voltage increased): **Movement detected** → We were NOT at minimum position
   - If `ΔV ≤ 100mV` (voltage didn't change): We might be at maximum position

5. **Proceed to find minimum position** by moving in the decreasing voltage direction
   - **AZ:** Move **Counter-Clockwise (CCW)**
   - **EL:** Move **DOWN**

### Phase 2: Find Minimum Voltage Position

**Goal:** Move to the position where voltage is at its minimum.

1. **Command movement in the direction that decreases voltage:**
   - **AZ:** Move **Counter-Clockwise (CCW)**
   - **EL:** Move **DOWN**

2. **Repeat the detection process:**
   - Move for 5 seconds
   - Measure voltage
   - Check if voltage is still decreasing
   - Continue until voltage change < 100mV (within noise threshold)

3. **Record minimum voltage** (Vmin)
   - This corresponds to:
     - **AZ:** 0° heading
     - **EL:** 0° elevation

### Phase 3: Find Maximum Voltage Position

**Goal:** Move to the position where voltage is at its maximum (~3.2V).

1. **Command movement in the direction that increases voltage:**
   - **AZ:** Move **Clockwise (CW)**
   - **EL:** Move **UP**

2. **Repeat the detection process:**
   - Move for 5 seconds
   - Measure voltage
   - Check if voltage is still increasing
   - Continue until voltage change < 100mV (within noise threshold)

3. **Record maximum voltage** (Vmax)
   - This corresponds to:
     - **AZ:** 180° heading
     - **EL:** 180° elevation

### Phase 4: Display Maximum Voltage

**Goal:** Show the user the measured maximum voltage for verification.

1. **Display Vmax on OLED for 3 seconds**
   - Shows: `CAL COMPLETE`
   - Shows: `Max V: X.XXV`

2. **User observes the value** to confirm it's close to the manually calibrated 3.2V

### Phase 5: Move to Half-Way Point

**Goal:** Position the rotator at the midpoint for user verification.

1. **Calculate half-way voltage:**
   - `Vhalf = (Vmin + Vmax) / 2`

2. **Estimate movement time:**
   - Based on observed motor speed during calibration
   - Time to half-way = (Vhalf - Vmin) / voltage_change_per_second

3. **Command movement** until Vhalf is reached
   - **AZ:** Move CW or CCW depending on current position
   - **EL:** Move UP or DOWN depending on current position

4. **Stop movement and measure voltage**
   - Verify voltage is within ±100mV of Vhalf

### Phase 6: User Confirmation

**Goal:** Ask user to verify the rotator is at the expected position.

1. **Display confirmation screen:**
   ```
   AT HALF-WAY
   AZ: XX°  EL: XX°
   Confirm?
   1.Yes 2.No
   ```

2. **User actions:**
   - **Btn1 (short press):** Confirm calibration is correct
   - **Btn2 (short press):** Reject calibration (start over)

3. **If confirmed:**
   - Save calibration values (Vmin, Vmax) to EEPROM
   - Display: `CAL SAVED`
   - Return to main menu

4. **If rejected:**
   - Return to Phase 1 and repeat calibration

### Calibration Data Stored

The following values are saved to EEPROM for voltage-to-angle conversion:

| Parameter | Description | Example |
|-----------|-------------|---------|
| Vmin | Minimum voltage (0° position) | 0.05V |
| Vmax | Maximum voltage (180° position) | 3.18V |
| Vrange | Voltage range (Vmax - Vmin) | 3.13V |
| Vhalf | Half-way voltage | 1.615V |

### Voltage-to-Angle Conversion Formula

```
angle = ((Vmeasured - Vmin) / Vrange) * 180
```

Where:
- `Vmeasured` = current voltage reading
- `Vmin` = minimum voltage (0° position)
- `Vrange` = Vmax - Vmin
- Result is in degrees (0-180°)

---

## Troubleshooting

### Calibration Fails to Detect Movement

**Possible causes:**
- DIN connector not properly connected
- Potentiometer wiring issue
- Motor not receiving power

**Solution:**
- Check DIN connector pins 6 (AZ) and 1 (EL)
- Verify 3.3V supply to ADS1115
- Check motor enable pins (D1 for AZ, D2 for EL)

### Voltage Reading Unstable

**Possible causes:**
- Loose wiring on potentiometer wiper
- Electrical noise from motors
- ADS1115 not calibrated

**Solution:**
- Check all DIN connector connections
- Ensure proper grounding
- Consider adding software filtering (average multiple readings)

### Maximum Voltage Too Low (< 3.0V)

**Possible causes:**
- Manual calibration not performed correctly
- Potentiometer worn or damaged
- Voltage drop in wiring

**Solution:**
- Repeat manual calibration with multimeter
- Check for voltage drop across DIN connector
- Verify potentiometer condition

### Maximum Voltage Too High (> 3.4V)

**Possible causes:**
- Manual calibration set too high
- Potentiometer resistance drift

**Solution:**
- Reduce manual calibration voltage to exactly 3.2V
- Replace potentiometer if drift persists

---

## Safety Notes

⚠️ **Always perform manual calibration before connecting the ESP unit**

- Never exceed 3.3V on ADS1115 input (hard limit)
- Monitor voltage during calibration (display shows real-time readings)
- If voltage exceeds 3.0V during automated calibration, stop and investigate
- Ensure rotator mechanical limits are not exceeded during movement

---

## Technical Specifications

| Parameter | Value |
|-----------|-------|
| ADS1115 Range | ±4.096V (GAIN_ONE) |
| Resolution | 0.125 mV/bit |
| Noise Threshold | 100mV |
| Movement Duration | 5 seconds |
| Target Max Voltage | 3.2V |
| Safe Voltage Range | 0.1V - 3.2V |

---

*Document Version: 1.0*  
*Last Updated: July 2026*
