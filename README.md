# AltDB

**AltDB** is an open-source, drop-in replacement library for the discontinued B&R `AsDB` library in Automation Studio 6.

It provides identical function block signatures, constants, error codes, and asynchronous execution semantics (`ERR_FUB_BUSY`), allowing existing Automation Studio codebases that depend on `AsDb` to run without application-level modifications.

---

## Current Testing & Status Disclaimer

> **Important:**  
> AltDB has currently been developed and tested **exclusively in B&R ARsim against Microsoft SQL Server 2022** running in a Linux container with database compatibility level 120 (SQL Server 2014 emulation).  
> It has **not yet been validated on physical SG4/SG4-ARM PLC hardware**, nor on physical SQL Server 2005–2014 installations. Community testing and feedback are welcome.

---

## Features

- **Direct Wire Protocol**: Clean-room implementation built against the published Microsoft Tabular Data Stream (`[MS-TDS]`) specification.
- **MIT License**: No FreeTDS or LGPL dependencies.
- **Drop-in Compatibility**: Retains exact legacy function block signatures:
  - `dbConnect()` / `dbConnectEnc()`
  - `dbExecuteSql()`
  - `dbFetchNextRow()`
  - `dbGetData()`
  - `dbGetAffectedRows()`
  - `dbGetColumnCount()`
  - `dbGetErrorMessage()`
  - `dbDisconnect()`
  - `dbGetInfo()`
- **Non-blocking Execution**: Seamlessly integrates into Automation Runtime cyclic tasks using standard non-blocking state loops returning `ERR_FUB_BUSY` (65535) until completion.

---

## Repository Structure

```text
├── AS_Project/                     # Full Automation Studio sample project
│   ├── Logical/
│   │   ├── Libraries/
│   │   │   └── AltDB/              # The AltDB drop-in library package
│   │   └── AltDBExample/           # Working reference recipe task
│   └── Physical/                   # Hardware configuration
├── AltDB.zip                       # Zipped AltDB drop-in library
└── README.md
