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
├── docker-compose.yml              # Local testing MSSQL instance
├── init.sql                        # Automatic creation of TestDb (level 120)
│   ├── Logical/
│   │   ├── Libraries/
│   │   │   └── AltDB/              # The AltDB drop-in library package
│   │   └── AltDBExample/           # Working reference recipe task
│   └── Physical/                   # Hardware configuration
└── README.md


## Using AltDB in an Existing Machine Project

If you already have a machine codebase that uses `AsDb`:

1. Download and unzip `AltDB.zip` from the latest [GitHub Release](../../releases).
2. In Automation Studio, delete the legacy `AsDb` library from your Logical View if it was not already removed during the AS 6 project upgrade.
3. Click the **Libraries** button on the Automation Studio toolbar, then select **Existing library...**.
4. Navigate to and select the extracted `AltDB` folder to import it into your project.
5. Rebuild your project. Existing task code, function block calls, and cyclic state machines remain unchanged.



## Testing with ARsim and Docker

The repository includes a complete Automation Studio sample project along with a preconfigured `docker-compose.yml` to make local testing straightforward:

### 1. Start the Test Database

Ensure Docker or Podman is installed. Run the following command from the repository root:

```bash
docker compose up -d
```

This spins up Microsoft SQL Server 2022 and automatically executes `init.sql`, which:
- Creates the `TestDb` database.
- Sets `COMPATIBILITY_LEVEL = 120` (SQL Server 2014 mode).
- Exposes the server on `localhost:1433`.

Default credentials:
- **Server**: `127.0.0.1:1433`
- **User**: `sa`
- **Password**: `YourStrong@Passw0rd`
- **Database**: `TestDb`

### 2. Run the Sample in Automation Studio

1. Open `AS_Project/Project.apj` in Automation Studio.
2. Ensure the active target configuration is set to **Simulation (ARsim)**.
3. Build and transfer the project to ARsim.
4. Open the Watch window for the `SQLRecipe` task:
   - Set `Command.eCommand := eCMD_CONNECT`.
   - The state machine will connect, create the sample `Recipes` table if it does not exist, seed a baseline recipe, and retrieve the stored entries.

To shut down the container after testing:

```bash
docker compose down -v
```