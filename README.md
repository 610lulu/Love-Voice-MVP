# Love Voice — Prototype Tests

This repository contains two separate technical prototype tests for the **Love Voice** interaction-design project.

## A Test — Physical Interaction

**Goal:** validate tangible input and device-state logic on Arduino Nano ESP32.

Tests:
- Heart button input
- Hold-to-Speak gesture
- Pull-to-adjust volume
- DAILY / SOS physical state transition
- SOS long-hold confirmation
- Optional Wi-Fi event logging

Folder: [`A_Physical_Interaction/`](A_Physical_Interaction/)

## B Test — Voice Pipeline

**Goal:** validate the voice pipeline from an authorised family voice sample to generated speech and ESP32 playback.

Pipeline:

`Authorised family voice sample -> voice cloning -> TTS -> PCM audio -> ESP32 -> I2S -> MAX98357A -> speaker`

Folder: [`B_Voice_Pipeline/`](B_Voice_Pipeline/)

## Prototype Boundary

These files are for **interaction-design and technical-feasibility testing only**. They are not medical-device software and should not be used for emergency-critical deployment.

For voice cloning, use only recordings from a person who has explicitly consented to creation and use of the voice model. Never commit API keys, Wi-Fi credentials, private recordings, or generated personal voice files to this repository.
