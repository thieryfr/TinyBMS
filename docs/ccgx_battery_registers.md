# Extrait registres Modbus GX pertinents pour batterie

| Service | Adresse | Description | Type | Échelle | Unité | Path DBus | Remarques |
|---|---|---|---|---|---|---|---|
| com.victronenergy.acsystem | 4940 | Ac input 1 current limit | uint16 | 10 | A | /Ac/In/1/CurrentLimit |  |
| com.victronenergy.acsystem | 4941 | Ac input 2 current limit | uint16 | 10 | A | /Ac/In/2/CurrentLimit |  |
| com.victronenergy.acsystem | 4942 | Ac current limit at grid meter | uint16 | 10 | A | /Settings/Ac/In/CurrentLimitEnergyMeter |  |
| com.victronenergy.alternator | 4100 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.alternator | 4101 | Battery current | int16 | 10 | A | /Dc/0/Current |  |
| com.victronenergy.battery | 256 | Battery power | int32 | 1 | W | /Dc/0/Power |  |
| com.victronenergy.battery | 258 | Battery power | int16 | 1 | W | /Dc/0/Power | Older 16-bit variant of 256 |
| com.victronenergy.battery | 259 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.battery | 260 | Starter battery voltage | uint16 | 100 | V DC | /Dc/1/Voltage |  |
| com.victronenergy.battery | 261 | Current | int16 | 10 | A DC | /Dc/0/Current | Postive: battery begin charged. Negative: battery being discharged |
| com.victronenergy.battery | 262 | Battery temperature | int16 | 10 | Degrees celsius | /Dc/0/Temperature | In degrees Celsius |
| com.victronenergy.battery | 263 | Mid-point voltage of the battery bank | uint16 | 100 | V DC | /Dc/0/MidVoltage |  |
| com.victronenergy.battery | 264 | Mid-point deviation of the battery bank | uint16 | 100 | % | /Dc/0/MidVoltageDeviation |  |
| com.victronenergy.battery | 266 | State of charge | uint16 | 10 | % | /Soc |  |
| com.victronenergy.battery | 280 | Relay status | uint16 | 1 | 0=Open;1=Closed | /Relay/0/State | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 281 | Deepest discharge | uint16 | -10 | Ah | /History/DeepestDischarge | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 282 | Last discharge | uint16 | -10 | Ah | /History/LastDischarge | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 283 | Average discharge | uint16 | -10 | Ah | /History/AverageDischarge | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 284 | Charge cycles | uint16 | 1 | count | /History/ChargeCycles | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 285 | Full discharges | uint16 | 1 | count | /History/FullDischarges | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 286 | Total Ah drawn | uint16 | -10 | Ah | /History/TotalAhDrawn | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 287 | Minimum voltage | uint16 | 100 | V DC | /History/MinimumVoltage | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 288 | Maximum voltage | uint16 | 100 | V DC | /History/MaximumVoltage | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 289 | Time since last full charge | uint16 | 0.01 | seconds | /History/TimeSinceLastFullCharge | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 290 | Automatic syncs | uint16 | 1 | count | /History/AutomaticSyncs | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 291 | Low voltage alarms | uint16 | 1 | count | /History/LowVoltageAlarms | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 292 | High voltage alarms | uint16 | 1 | count | /History/HighVoltageAlarms | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 293 | Low starter voltage alarms | uint16 | 1 | count | /History/LowStarterVoltageAlarms | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 294 | High starter voltage alarms | uint16 | 1 | count | /History/HighStarterVoltageAlarms | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 295 | Minimum starter voltage | uint16 | 100 | V DC | /History/MinimumStarterVoltage | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 296 | Maximum starter voltage | uint16 | 100 | V DC | /History/MaximumStarterVoltage | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 301 | Discharged Energy | uint16 | 10 | kWh | /History/DischargedEnergy | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 302 | Charged Energy | uint16 | 10 | kWh | /History/ChargedEnergy | Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 303 | Time to go | uint16 | 0.01 | seconds | /TimeToGo | Special value: 0 = charging. Not supported by CAN.Bus BMS batteries. |
| com.victronenergy.battery | 304 | State of health | uint16 | 10 | % | /Soh | Not supported by Victron products. Supported by CAN.Bus batteries. |
| com.victronenergy.battery | 1289 | System; number of cells per battery | uint16 | 1 | count | /System/NrOfCellsPerBattery |  |
| com.victronenergy.battery | 1323 | SmartLithium error flag: Battery number | uint16 | 1 |  | /Errors/SmartLithium/NrOfBatteries |  |
| com.victronenergy.battery | 1328 | Connection information | string[8] |  |  | /ConnectionInformation | Information from a managed battery about alternate connection information, such as a URL or IP address. |
| com.victronenergy.charger | 2316 | AC Current limit | int16 | 10 | A AC | /Ac/In/CurrentLimit |  |
| com.victronenergy.dcdc | 4804 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.dcdc | 4805 | Battery current | int16 | 10 | A DC | /Dc/0/Current |  |
| com.victronenergy.dcdc | 4806 | Battery temperature | int16 | 10 | Degrees celsius | /Dc/0/Temperature |  |
| com.victronenergy.dcgenset | 5213 | Starter battery voltage | uint16 | 100 | DC V | /StarterVoltage |  |
| com.victronenergy.dcload | 4300 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.dcload | 4301 | Battery current | int16 | 10 | A | /Dc/0/Current |  |
| com.victronenergy.dcsource | 4200 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.dcsource | 4201 | Battery current | int16 | 10 | A | /Dc/0/Current |  |
| com.victronenergy.dcsystem | 4400 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.dcsystem | 4401 | Battery current | int16 | 10 | A | /Dc/0/Current |  |
| com.victronenergy.fuelcell | 4000 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.fuelcell | 4001 | Battery current | int16 | 10 | A | /Dc/0/Current |  |
| com.victronenergy.inverter | 3105 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.inverter | 3106 | Battery current | int16 | 10 | A DC | /Dc/0/Current |  |
| com.victronenergy.inverter | 3111 | High battery voltage alarm | uint16 | 1 | 0=No alarm;1=Warning;2=Alarm | /Alarms/HighVoltage |  |
| com.victronenergy.inverter | 3114 | Low battery voltage alarm | uint16 | 1 | 0=No alarm;1=Warning;2=Alarm | /Alarms/LowVoltage |  |
| com.victronenergy.inverter | 3130 | Energy from battery to AC-out | uint32 | 100 | kWh | /Energy/InverterToAcOut |  |
| com.victronenergy.inverter | 3132 | Energy from AC-out to battery | uint32 | 100 | kWh | /Energy/OutToInverter |  |
| com.victronenergy.inverter | 3136 | Energy from solar to battery | uint32 | 100 | kWh | /Energy/SolarToBattery |  |
| com.victronenergy.motordrive | 2052 | Controller DC Power | int16 | 10 | W | /Dc/0/Power | Positive = being powered from battery, Negative is charging battery (regeneration) |
| com.victronenergy.multi | 4522 | Ac input 1 current limit | uint16 | 10 | A | /Ac/In/1/CurrentLimit |  |
| com.victronenergy.multi | 4523 | Ac input 2 current limit | uint16 | 10 | A | /Ac/In/2/CurrentLimit |  |
| com.victronenergy.multi | 4526 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.multi | 4527 | Battery current | int16 | 10 | A DC | /Dc/0/Current |  |
| com.victronenergy.multi | 4528 | Battery temperature | int16 | 10 | Degrees celsius | /Dc/0/Temperature |  |
| com.victronenergy.multi | 4529 | Battery State of Charge | uint16 | 10 | % | /Soc |  |
| com.victronenergy.multi | 4535 | Low battery temperature alarm | uint16 | 1 | 0=Ok;1=Warning;2=Alarm | /Alarms/LowTemperature |  |
| com.victronenergy.multi | 4548 | Energy from AC-in-1 to battery | uint32 | 100 | kWh | /Energy/AcIn1ToInverter |  |
| com.victronenergy.multi | 4552 | Energy from AC-in-2 to battery | uint32 | 100 | kWh | /Energy/AcIn2ToInverter |  |
| com.victronenergy.multi | 4558 | Energy from battery to AC-in-1 | uint32 | 100 | kWh | /Energy/InverterToAcIn1 |  |
| com.victronenergy.multi | 4560 | Energy from battery to AC-in-2 | uint32 | 100 | kWh | /Energy/InverterToAcIn2 |  |
| com.victronenergy.multi | 4562 | Energy from battery to AC-out | uint32 | 100 | kWh | /Energy/InverterToAcOut |  |
| com.victronenergy.multi | 4564 | Energy from AC-out to battery | uint32 | 100 | kWh | /Energy/OutToInverter |  |
| com.victronenergy.multi | 4572 | Energy from solar to battery | uint32 | 100 | kWh | /Energy/SolarToBattery |  |
| com.victronenergy.settings | 2710 | Limit managed battery voltage | uint16 | 10 | V DC | /Settings/SystemSetup/MaxChargeVoltage | Only used if there is a managed battery in the system |
| com.victronenergy.settings | 2900 | ESS BatteryLife state | uint16 | 1 | 0=Unused, BL disabled;1=Restarting;2=Self-consumption;3=Self-consumption;4=Self-consumption;5=Discharge disabled;6=Force charge;7=Sustain;8=Low Soc Recharge;9=Keep batteries charged;10=BL Disabled;11=BL Disabled (Low SoC);12=BL Disabled (Low SOC recharge) | /Settings/CGwacs/BatteryLife/State | Use value 0 (disable) and 1(enable) for writing only |
| com.victronenergy.settings | 2903 | ESS BatteryLife SoC limit (read only) | uint16 | 10 | % | /Settings/Cgwacs/BatteryLife/SocLimit | This value is maintained by BatteryLife. The Active SOC limit is the lower of this value, and register 2901. Also see https://www.victronenergy.com/media/pg/Energy_Storage_System/en/controlling-depth-of-discharge.html#UUID-af4a7478-4b75-68ac-cf3c-16c381335d1e |
| com.victronenergy.settings | 5420 | Battery capacity | uint16 | 10 | kWh | /Settings/DynamicEss/BatteryCapacity |  |
| com.victronenergy.settings | 5421 | Full battery charge duration | uint16 | 1 | hour | /Settings/DynamicEss/FullChargeDuration |  |
| com.victronenergy.settings | 5422 | Full battery charge interval | uint16 | 1 | day | /Settings/DynamicEss/FullChargeInterval |  |
| com.victronenergy.solarcharger | 771 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.solarcharger | 772 | Battery current | int16 | 10 | A DC | /Dc/0/Current |  |
| com.victronenergy.solarcharger | 773 | Battery temperature | int16 | 10 | Degrees celsius | /Dc/0/Temperature | VE.Can MPPTs only |
| com.victronenergy.system | 840 | Battery Voltage (System) | uint16 | 10 | V DC | /Dc/Battery/Voltage | Battery Voltage determined from different measurements. In order of preference: BMV-voltage (V), Multi-DC-Voltage (CV), MPPT-DC-Voltage (ScV), Charger voltage |
| com.victronenergy.system | 841 | Battery Current (System) | int16 | 10 | A DC | /Dc/Battery/Current | Postive: battery begin charged. Negative: battery being discharged |
| com.victronenergy.system | 842 | Battery Power (System) | int16 | 1 | W | /Dc/Battery/Power | Postive: battery begin charged. Negative: battery being discharged |
| com.victronenergy.system | 843 | Battery State of Charge (System) | uint16 | 1 | % | /Dc/Battery/Soc | Best battery state of charge, determined from different measurements. |
| com.victronenergy.system | 844 | Battery state (System) | uint16 | 1 | 0=idle;1=charging;2=discharging | /Dc/Battery/State |  |
| com.victronenergy.system | 845 | Battery Consumed Amphours (System) | uint16 | -10 | Ah | /Dc/Battery/ConsumedAmphours |  |
| com.victronenergy.system | 846 | Battery Time to Go (System) | uint16 | 0.01 | s | /Dc/Battery/TimeToGo | Special value: 0 = charging |
| com.victronenergy.system | 860 | DC System Power | int16 | 1 | W | /Dc/System/Power | Power supplied by Battery to system. |
| com.victronenergy.temperature | 3307 | Sensor battery voltage | uint16 | 100 | V | /BatteryVoltage | Used by wireless tags that have a battery in the sensor |
| com.victronenergy.vebus | 22 | Active input current limit | int16 | 10 | A | /Ac/ActiveIn/CurrentLimit | See Venus-OS manual for limitations, for example when VE.Bus BMS or DMC is installed. |
| com.victronenergy.vebus | 26 | Battery voltage | uint16 | 100 | V DC | /Dc/0/Voltage |  |
| com.victronenergy.vebus | 27 | Battery current | int16 | 10 | A DC | /Dc/0/Current | Positive: current flowing from the Multi to the dc system. Negative: the other way around. |
| com.victronenergy.vebus | 30 | VE.Bus state of charge | uint16 | 10 | % | /Soc |  |
| com.victronenergy.vebus | 33 | Switch Position | uint16 | 1 | 1=Charger Only;2=Inverter Only;3=On;4=Off | /Mode | See Venus-OS manual for limitations, for example when VE.Bus BMS or DMC is installed. |
| com.victronenergy.vebus | 35 | Low battery alarm | uint16 | 1 | 0=Ok;1=Warning;2=Alarm | /Alarms/LowBattery |  |
| com.victronenergy.vebus | 45 | Low battery alarm L1 | uint16 | 1 | 0=Ok;1=Warning;2=Alarm | /Alarms/L1/LowBattery |  |
| com.victronenergy.vebus | 49 | Low battery alarm L2 | uint16 | 1 | 0=Ok;1=Warning;2=Alarm | /Alarms/L2/LowBattery |  |
| com.victronenergy.vebus | 53 | Low battery alarm L3 | uint16 | 1 | 0=Ok;1=Warning;2=Alarm | /Alarms/L3/LowBattery |  |
| com.victronenergy.vebus | 57 | VE.Bus BMS allows battery to be charged | uint16 | 1 | 0=No;1=Yes | /Bms/AllowToCharge | VE.Bus BMS allows the battery to be charged |
| com.victronenergy.vebus | 58 | VE.Bus BMS allows battery to be discharged | uint16 | 1 | 0=No;1=Yes | /Bms/AllowToDischarge | VE.Bus BMS allows the battery to be discharged |
| com.victronenergy.vebus | 59 | VE.Bus BMS is expected | uint16 | 1 | 0=No;1=Yes | /Bms/BmsExpected | Presence of VE.Bus BMS is expected based on vebus settings (presence of ESS or BMS assistant) |
| com.victronenergy.vebus | 60 | VE.Bus BMS error | uint16 | 1 | 0=No;1=Yes | /Bms/Error |  |
| com.victronenergy.vebus | 61 | Battery temperature | int16 | 10 | Degrees celsius | /Dc/0/Temperature |  |
| com.victronenergy.vebus | 76 | Energy from AC-In 1 to battery | uint32 | 100 | kWh | /Energy/AcIn1ToInverter | Energy counters from the Multi(s) are volatile. |
| com.victronenergy.vebus | 80 | Energy from AC-In 2 to battery | uint32 | 100 | kWh | /Energy/AcIn2ToInverter | These energy counters ALSO reset to zero when the GX-device reboots. |
| com.victronenergy.vebus | 86 | Energy from battery to AC-in 1 | uint32 | 100 | kWh | /Energy/InverterToAcIn1 |  |
| com.victronenergy.vebus | 88 | Energy from battery to AC-in 2 | uint32 | 100 | kWh | /Energy/InverterToAcIn2 |  |
| com.victronenergy.vebus | 90 | Energy from battery to AC-out | uint32 | 100 | kWh | /Energy/InverterToAcOut |  |
| com.victronenergy.vebus | 92 | Energy from AC-out to battery (typically from PV-inverter) | uint32 | 100 | kWh | /Energy/OutToInverter |  |
