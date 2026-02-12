

---

# C-FLAT: Control-Flow Attestation for Embedded Systems

This repository contains a prototype implementation of **C-FLAT (Control-FLow ATtestation)**, a hardware-assisted remote attestation scheme that enables a verifier to learn the exact execution path of an application. This implementation is designed for the **Raspberry Pi** using **ARM TrustZone** (via OP-TEE) and **LLVM** for static instrumentation.

## 🚀 Project Overview

Standard static attestation only verifies that benign code is loaded. C-FLAT extends this by attesting the **runtime execution details**, protecting against attacks that hijack control-flow (e.g., ROP, JOP) or data-flow (non-control-data attacks).

### Core Components

1. **LLVM Instrumentation Pass**: Statically analyzes the CFG and inserts measurement hooks into basic blocks and loop headers.


2. **Measurement Engine (TEE)**: A Trusted Application (TA) running in the **Secure World** (OP-TEE) that maintains the cumulative hash chain and manages the loop-measurement stack.


3. **Syringe Pump Case Study**: A real-world cyber-physical system application used to demonstrate the detection of unauthorized physical actions through software attestation.



## 📊 Key Observations & Results

The following observations were made during the evaluation of the **Open Syringe Pump** benchmark:

### 1. Verification of Physical Integrity

By handling loops as separate sub-programs, C-FLAT can verify the amount of work done by the hardware.

* 10μL Movement: Recorded 68 iterations in the motor loop.
* 20μL Movement: Recorded 136 iterations.
 
> **Significance**:These counts match the physical calculation (Volume×ustepsPerML). A mismatch here detects non-control-data attacks where an attacker alters the dispensed volume without changing the code.
> 
> 

### 2. Path-Level Security

* **Unique Fingerprinting**: Every unique sequence of commands ("+", "10", "20") produces a distinct **Final Hash**.
* **ROP Detection**: The system successfully prevents Return-Oriented Programming by matching function call-return edges in the Secure World.
* **Efficiency**: The system reliably managed **over 4,000 World Switches** between the Normal and Secure worlds during high-frequency motor operations.

## 📸 Screenshots

Below are the attestation logs captured during runtime:

### Attestation Summary (Command: "10")

<img width="1084" height="670" alt="Bolus0 2" src="https://github.com/user-attachments/assets/4e4c6759-3989-485a-9f59-77144971ac2a" />

*Displays the loop IDs and the 68 iterations corresponding to the  bolus.*

### Control-Flow Path Analysis (Command: "+")

<img width="1084" height="701" alt="command_+" src="https://github.com/user-attachments/assets/ebf92772-cda8-46da-806e-50b9923488e3" />

*Shows the unique Final Hash and the increased world switch count during motor movement.*

### Debugging & System Health

<img width="1084" height="701" alt="buffer_log_overflow" src="https://github.com/user-attachments/assets/51a21420-90ed-4907-82f0-bdd28c2045ae" />

*Observation of buffer management limits in the TEE before optimizing the logging granularity.*

## 🛠 Setup & Build

### Prerequisites

* **LLVM/Clang**: For the instrumentation pass.
* **OP-TEE Developer Environment**: For compiling the Trusted Application.
* **Cross-Compiler**: `arm-linux-gnueabihf-` for Raspberry Pi targets.

### Building the Project

```bash
# Build the LLVM Pass
cd instrumentation/llvm-pass/build
cmake ..
make

# Build the Trusted Application and Syringe App
cd ../../../scripts
./build_all.sh

```

## 📜 Acknowledgments

This project is based on the research paper: *C-FLAT: Control-Flow Attestation for Embedded Systems Software*.

---
