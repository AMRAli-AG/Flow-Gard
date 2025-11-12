 Flow Guard – Smart Water Monitoring (MVP Firmware)

Description:
This repository contains the embedded firmware for the Flow Guard MVP, a smart IoT system designed to monitor real-time water consumption and detect leaks in residential and commercial buildings. The project integrates a Modbus water meter, a RAKwireless LoRaWAN module, and a ThingsBoard IoT cloud platform for data visualization and analytics.

Core Features:


Modbus Integration: Reads water usage and flow data from a Modbus-based ultrasonic water meter.

LoRaWAN Communication: Transmits sensor data securely to a RAK Gateway.

Cloud Connectivity: Publishes readings to ThingsBoard, enabling real-time dashboards and alerting rules.

Built on Zephyr RTOS: Modular, scalable, and real-time embedded firmware base.

Version-Controlled & Documented: Clean, structured firmware with detailed documentation and configuration files.



Tech Stack:

Zephyr RTOS

C / C++

Modbus RTU

LoRaWAN (RAKwireless WisBlock)

ThingsBoard IoT Platform

Goal:
Deliver a complete, functional MVP firmware enabling real-time water monitoring, cloud integration, and remote management — forming the foundation for Flow Guard’s scalable IoT solution.
