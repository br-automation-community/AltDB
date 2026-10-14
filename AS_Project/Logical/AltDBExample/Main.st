(********************************************************************
 * COPYRIGHT --  
 ********************************************************************
 * Program: SQLRecipe
 * File: SQLRecipe.st
 * Author: B&R
 * Created: December 27, 2010
 ********************************************************************
 * Implementation of program SQLRecipe
 ********************************************************************)

PROGRAM _INIT


(* TODO : Add your code here *)


END_PROGRAM


PROGRAM _CYCLIC

	CASE Command.eCommand OF
		eCMD_IDLE :
		eCMD_CONNECT :
			IF eStep = eSTOP THEN
				eStep := eCONNECT;
				Command.eCommand := eCMD_CONNECT_w;
			END_IF
		eCMD_CONNECT_w:
			IF eStep = eWAIT_FOR_COMMAND THEN
				Command.eCommand := eCMD_IDLE;
			END_IF
		eCMD_LOAD :
			IF eStep = eWAIT_FOR_COMMAND THEN
				eStep := eLOAD_RECEIPE;
				Command.eCommand := eCMD_LOAD_w;
			END_IF
		eCMD_LOAD_w:
			IF eStep = eWAIT_FOR_COMMAND THEN
				Command.eCommand := eCMD_IDLE;
			END_IF
		eCMD_SAVE :
			IF eStep = eWAIT_FOR_COMMAND THEN
				eStep := eSAVE_RECEIPE;
				Command.eCommand := eCMD_SAVE_w;
			END_IF
		eCMD_SAVE_w:
			IF eStep = eWAIT_FOR_COMMAND THEN
				Command.eCommand := eCMD_IDLE;
			END_IF
		eCMD_SAVEAS :
			IF eStep = eWAIT_FOR_COMMAND THEN
				eStep := eSAVE_AS;
				Command.eCommand := eCMD_SAVEAS_w;
			END_IF
		eCMD_SAVEAS_w:
			IF eStep = eWAIT_FOR_COMMAND THEN
				Command.eCommand := eCMD_IDLE;
			END_IF			
		eCMD_DELETE :
			IF eStep = eWAIT_FOR_COMMAND THEN
				eStep := eDELETE_RECEIPE;
				Command.eCommand := eCMD_DELETE_w;
			END_IF
		eCMD_DELETE_w:
			IF eStep = eWAIT_FOR_COMMAND THEN
				Command.eCommand := eCMD_IDLE;
			END_IF
		eCMD_RENAME :
			IF eStep = eWAIT_FOR_COMMAND THEN
				eStep := eRENAME_RECEIPE;
				Command.eCommand := eCMD_RENAME_w;
			END_IF
		eCMD_RENAME_w:
			IF eStep = eWAIT_FOR_COMMAND THEN
				Command.eCommand := eCMD_IDLE;
			END_IF
		eCMD_DISCONNECT :
			IF eStep = eWAIT_FOR_COMMAND OR
				eStep = eERROR THEN
				eStep := eDISCONNECT;
				Command.eCommand := eCMD_DISCONNECT_w;
			END_IF
		eCMD_DISCONNECT_w:
			IF eStep = eSTOP THEN
				Command.eCommand := eCMD_IDLE;
			END_IF			
	END_CASE	

	CASE eStep OF
		(* connect to database *)
		eCONNECT :
			brsmemset( ADR(ReceipeDBConnect), 0, SIZEOF(ReceipeDBConnect) );
			ReceipeDBConnect.enable := TRUE;
			ReceipeDBConnect.databaseSystem := DB_SYSTEM_MS_SQL;
			ReceipeDBConnect.pUserName := ADR(USERNAME);
			ReceipeDBConnect.pPassword := ADR(PASSWORD);
			ReceipeDBConnect.pServerName := ADR(HOST);
			ReceipeDBConnect.pDatabaseName := ADR(DATABASE);
			eStep := eCONNECT_r;
		eCONNECT_r :
			ReceipeDBConnect();
			IF ReceipeDBConnect.status = 0 THEN
				ConnectionIdent := ReceipeDBConnect.connectionIdent;
				eStep := eGET_TABLES;			
			ELSIF ReceipeDBConnect.status <> 65535 THEN
				IF ReceipeDBConnect.connectionIdent <> 0 THEN
					ConnectionIdent := ReceipeDBConnect.connectionIdent;
					eStep := eGET_ERROR;
				ELSE
					eStep := eERROR;
				END_IF
			END_IF	
			
			(* read out available tables *)
		eGET_TABLES :
			brsstrcpy( ADR(sSQLstring), ADR('SELECT name FROM sysobjects WHERE type=$'u$'') );
			ReceipeDBExecute.enable := TRUE;
			ReceipeDBExecute.pSqlStatement := ADR(sSQLstring);
			ReceipeDBExecute.connectionIdent := ConnectionIdent;
			eStep := eGET_TABLES_r;
		eGET_TABLES_r :
			ReceipeDBExecute();
			IF ReceipeDBExecute.status = ERR_OK THEN
				eStep := eFETCH_NEXT_ROW;
			ELSIF ReceipeDBExecute.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			(* prepare one row to read column(s) *)
		eFETCH_NEXT_ROW :
			ReceipeDBFetchNextRow.enable := TRUE;
			ReceipeDBFetchNextRow.connectionIdent := ConnectionIdent;
			eStep := eFETCH_NEXT_ROW_r;
		eFETCH_NEXT_ROW_r :
			ReceipeDBFetchNextRow();
			IF ReceipeDBFetchNextRow.status = ERR_OK THEN
				eStep := eREAD_TABLE_NAME;
			ELSIF ReceipeDBFetchNextRow.status <> ERR_FUB_BUSY THEN
				IF ReceipeDBFetchNextRow.dbError = 100 THEN
					eStep := eCREATE_TABLE;
				ELSE				
					eStep := eGET_ERROR;
				END_IF
			END_IF
			(* read out 1st column -> name *)
		eREAD_TABLE_NAME :
			ReceipeDBGetData.enable := TRUE;
			ReceipeDBGetData.connectionIdent := ConnectionIdent;
			ReceipeDBGetData.columnIdx := 1;
			ReceipeDBGetData.pData := ADR( sData );
			ReceipeDBGetData.dataSize := SIZEOF( sData );
			ReceipeDBGetData.dataType := DB_SQL_CHAR;
			eStep := eREAD_TABLE_NAME_r;
		eREAD_TABLE_NAME_r :
			ReceipeDBGetData();
			IF ReceipeDBGetData.status = 0 THEN
				IF brsstrcmp( ADR(sData), ADR(TABLE) )=0 THEN
					eStep := eGET_RECEIPE_NAMES;
				ELSE
					eStep := eFETCH_NEXT_ROW;
				END_IF
			ELSIF ReceipeDBGetData.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			
			(* Create New Table *)
		eCREATE_TABLE :
			brsstrcpy( ADR(sSQLstring), ADR('CREATE TABLE '));
			brsstrcat( ADR(sSQLstring), ADR(TABLE));
			brsstrcat( ADR(sSQLstring), ADR(' (') );
			brsstrcat( ADR(sSQLstring), ADR('idx smallint IDENTITY (1,1) PRIMARY KEY, ') );
			brsstrcat( ADR(sSQLstring), ADR('name varchar(') );
			brsitoa(LENGTH_NAME, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );
			brsstrcat( ADR(sSQLstring), ADR(') UNIQUE, ') );
			brsstrcat( ADR(sSQLstring), ADR('price real, ') );
			brsstrcat( ADR(sSQLstring), ADR('setTemp real, ') );
			brsstrcat( ADR(sSQLstring), ADR('milk real, ') );
			brsstrcat( ADR(sSQLstring), ADR('sugar real, ') );
			brsstrcat( ADR(sSQLstring), ADR('coffee real, ') );
			brsstrcat( ADR(sSQLstring), ADR('water real ') );
			brsstrcat( ADR(sSQLstring), ADR(')') );
			ReceipeDBExecute.enable := TRUE;
			ReceipeDBExecute.pSqlStatement := ADR(sSQLstring);
			ReceipeDBExecute.connectionIdent := ConnectionIdent;
			eStep := eCREATE_TABLE_r;
		eCREATE_TABLE_r :
			ReceipeDBExecute();
			IF ReceipeDBExecute.status = ERR_OK THEN
				eStep := eINSERT_DEFAULT;
			ELSIF ReceipeDBExecute.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			(* fill default receipe *)
		eINSERT_DEFAULT :
			brsstrcpy( ADR(sSQLstring), ADR('INSERT INTO ') );
			brsstrcat( ADR(sSQLstring), ADR(TABLE) );
			brsstrcat( ADR(sSQLstring), ADR(' (name, price, setTemp, milk, sugar, coffee, water) ') );
			brsstrcat( ADR(sSQLstring), ADR(' VALUES ($'default$',$'1.73$',$'80.0$',$'95.0$',$'33.3$',$'66.6$',$'155.3$')') );
			ReceipeDBExecute.enable := TRUE;
			ReceipeDBExecute.pSqlStatement := ADR(sSQLstring);
			ReceipeDBExecute.connectionIdent := ConnectionIdent;
			eStep := eINSERT_DEFAULT_r;
		eINSERT_DEFAULT_r :
			ReceipeDBExecute();
			IF ReceipeDBExecute.status = ERR_OK THEN
				eStep := eGET_RECEIPE_NAMES;
			ELSIF ReceipeDBExecute.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			
			(* get receipe names - only as much as can saved in list *)
		eGET_RECEIPE_NAMES :
			brsstrcpy( ADR(sSQLstring), ADR('SELECT TOP(') );
			brsitoa( SIZEOF(Command.sReceipeNames)/SIZEOF(Command.sReceipeNames[0]), ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );
			brsstrcat( ADR(sSQLstring), ADR(') name FROM ') );
			brsstrcat( ADR(sSQLstring), ADR(TABLE) );
			brsstrcat( ADR(sSQLstring), ADR(' ORDER BY name ASC') );
			ReceipeDBExecute.enable := TRUE;
			ReceipeDBExecute.pSqlStatement := ADR(sSQLstring);
			ReceipeDBExecute.connectionIdent := ConnectionIdent;
			ReceipeNameCounter := 0;
			brsmemset( ADR(Command.sReceipeNames), 0, SIZEOF(Command.sReceipeNames) );
			eStep := eGET_RECEIPE_NAMES_r;
		eGET_RECEIPE_NAMES_r:
			ReceipeDBExecute();
			IF ReceipeDBExecute.status = ERR_OK THEN
				eStep := eFETCH_RECEIPE_NAME;
			ELSIF ReceipeDBExecute.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			(* prepare one row to read column(s) *)
		eFETCH_RECEIPE_NAME :
			ReceipeDBFetchNextRow.enable := TRUE;
			ReceipeDBFetchNextRow.connectionIdent := ConnectionIdent;
			eStep := eFETCH_RECEIPE_NAME_r;
		eFETCH_RECEIPE_NAME_r :
			ReceipeDBFetchNextRow();
			IF ReceipeDBFetchNextRow.status = ERR_OK THEN
				eStep := eREAD_RECEIPE_NAME;
			ELSIF ReceipeDBFetchNextRow.status <> ERR_FUB_BUSY THEN
				IF ReceipeDBFetchNextRow.dbError = DB_SQL_NO_DATA THEN
					IF ReceipeNameCounter = 0 THEN	(* no receipe - maybe last receipe deleted *)
						eStep := eINSERT_DEFAULT;
					ELSE
						eStep := eWAIT_FOR_COMMAND;	(* all receipes read - wait for command *)
					END_IF
				ELSE
					eStep := eGET_ERROR;
				END_IF
			END_IF
			(* read out 1st column -> name *)
		eREAD_RECEIPE_NAME :
			ReceipeDBGetData.enable := TRUE;
			ReceipeDBGetData.connectionIdent := ConnectionIdent;
			ReceipeDBGetData.columnIdx := 1;
			ReceipeDBGetData.pData := ADR( Command.sReceipeNames[ReceipeNameCounter] );
			ReceipeDBGetData.dataSize := SIZEOF(Command.sReceipeNames[0]) - 1;
			ReceipeDBGetData.dataType := DB_SQL_CHAR;
			eStep := eREAD_RECEIPE_NAME_r;
		eREAD_RECEIPE_NAME_r :
			ReceipeDBGetData();
			IF ReceipeDBGetData.status = 0 THEN
				ReceipeNameCounter := ReceipeNameCounter + 1;
				IF ReceipeNameCounter < (SIZEOF(Command.sReceipeNames)/SIZEOF(Command.sReceipeNames[0])) THEN
					eStep := eFETCH_RECEIPE_NAME;
				ELSE
					Command.uStatus := 0;
					eStep := eWAIT_FOR_COMMAND;
				END_IF				
			ELSIF ReceipeDBGetData.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
		
		eWAIT_FOR_COMMAND :
		
			(* Load a receipe *)
		eLOAD_RECEIPE :
			brsstrcpy( ADR(sSQLstring), ADR('SELECT price, setTemp, milk, sugar, coffee, water FROM ') );
			brsstrcat( ADR(sSQLstring), ADR(TABLE) );
			brsstrcat( ADR(sSQLstring), ADR(' WHERE name = $'') );
			brsstrcat( ADR(sSQLstring), ADR(Command.sNewReceipe) );
			brsstrcat( ADR(sSQLstring), ADR('$'') );
			ReceipeDBExecute.enable := TRUE;
			ReceipeDBExecute.pSqlStatement := ADR(sSQLstring);
			ReceipeDBExecute.connectionIdent := ConnectionIdent;
			eStep := eLOAD_RECEIPE_r;
		eLOAD_RECEIPE_r :
			ReceipeDBExecute();
			IF ReceipeDBExecute.status = ERR_OK THEN
				eStep := eFETCH_VALUES;
			ELSIF ReceipeDBExecute.status <> ERR_FUB_BUSY THEN
				IF ReceipeDBExecute.dbError = 100 THEN
					eStep := eWAIT_FOR_COMMAND;
				ELSE
					eStep := eGET_ERROR;
				END_IF
			END_IF
			(* prepare one row to read column(s) *)
		eFETCH_VALUES :
			ReceipeDBFetchNextRow.enable := TRUE;
			ReceipeDBFetchNextRow.connectionIdent := ConnectionIdent;
			eStep := eFETCH_VALUES_r;
		eFETCH_VALUES_r :
			ReceipeDBFetchNextRow();
			IF ReceipeDBFetchNextRow.status = ERR_OK THEN
				usCol := 1;
				eStep := eGET_VALUES;
			ELSIF ReceipeDBFetchNextRow.status <> ERR_FUB_BUSY THEN
				IF ReceipeDBFetchNextRow.dbError = DB_SQL_NO_DATA THEN
					Command.uStatus := 1;
					eStep := eWAIT_FOR_COMMAND;
				ELSE
					eStep := eGET_ERROR;
				END_IF
			END_IF
			(* read out all columns *)
		eGET_VALUES :
			ReceipeDBGetData.enable := TRUE;
			ReceipeDBGetData.connectionIdent := ConnectionIdent;
			ReceipeDBGetData.columnIdx := usCol;
			CASE usCol OF
				1:			
					ReceipeDBGetData.pData := ADR(gMainLogic.par.receipe.price);
					ReceipeDBGetData.dataSize := SIZEOF(gMainLogic.par.receipe.price);
					ReceipeDBGetData.dataType := DB_SQL_REAL;
				2:
					ReceipeDBGetData.pData := ADR(gMainLogic.par.receipe.setTemp);
					ReceipeDBGetData.dataSize := SIZEOF(gMainLogic.par.receipe.setTemp);
					ReceipeDBGetData.dataType := DB_SQL_REAL;
				3:
					ReceipeDBGetData.pData := ADR(gMainLogic.par.receipe.milk);
					ReceipeDBGetData.dataSize := SIZEOF(gMainLogic.par.receipe.milk);
					ReceipeDBGetData.dataType := DB_SQL_REAL;
				4:
					ReceipeDBGetData.pData := ADR(gMainLogic.par.receipe.sugar);
					ReceipeDBGetData.dataSize := SIZEOF(gMainLogic.par.receipe.sugar);
					ReceipeDBGetData.dataType := DB_SQL_REAL;
				5:
					ReceipeDBGetData.pData := ADR(gMainLogic.par.receipe.coffee);
					ReceipeDBGetData.dataSize := SIZEOF(gMainLogic.par.receipe.coffee);
					ReceipeDBGetData.dataType := DB_SQL_REAL;
				6:
					ReceipeDBGetData.pData := ADR(gMainLogic.par.receipe.water);
					ReceipeDBGetData.dataSize := SIZEOF(gMainLogic.par.receipe.water);
					ReceipeDBGetData.dataType := DB_SQL_REAL;
			END_CASE
			eStep := eGET_VALUES_r;
		eGET_VALUES_r :
			ReceipeDBGetData();
			IF ReceipeDBGetData.status = 0 THEN
				usCol := usCol + 1;
				IF usCol <= 6 THEN
					eStep := eGET_VALUES;
				ELSE
					Command.uStatus := ERR_OK;
					brsstrcpy( ADR(Command.sActReceipe), ADR(Command.sNewReceipe) );
					eStep := eWAIT_FOR_COMMAND;
				END_IF				
			ELSIF ReceipeDBGetData.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			
			(* save receipe *)
		eSAVE_RECEIPE :
			brsstrcpy( ADR(sSQLstring), ADR('UPDATE ') );
			brsstrcat( ADR(sSQLstring), ADR(TABLE) );
			brsstrcat( ADR(sSQLstring), ADR(' SET price=$'') );
			brsftoa( gMainLogic.par.receipe.price, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );
			brsstrcat( ADR(sSQLstring), ADR('$', setTemp=$'') );
			brsftoa( gMainLogic.par.receipe.setTemp, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );
			brsstrcat( ADR(sSQLstring), ADR('$', milk=$'') );
			brsftoa( gMainLogic.par.receipe.milk, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );
			brsstrcat( ADR(sSQLstring), ADR('$', sugar=$'') );
			brsftoa( gMainLogic.par.receipe.sugar, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );
			brsstrcat( ADR(sSQLstring), ADR('$', coffee=$'') );
			brsftoa( gMainLogic.par.receipe.coffee, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );
			brsstrcat( ADR(sSQLstring), ADR('$', water=$'') );
			brsftoa( gMainLogic.par.receipe.water, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );
			brsstrcat( ADR(sSQLstring), ADR('$' WHERE name=$'') );
			brsstrcat( ADR(sSQLstring), ADR(Command.sActReceipe) );
			brsstrcat( ADR(sSQLstring), ADR('$'') );
			ReceipeDBExecute.enable := TRUE;
			ReceipeDBExecute.pSqlStatement := ADR(sSQLstring);
			ReceipeDBExecute.connectionIdent := ConnectionIdent;
			eStep := eSAVE_RECEIPE_r;
		eSAVE_RECEIPE_r :
			ReceipeDBExecute();
			IF ReceipeDBExecute.status = ERR_OK THEN
				eStep := eSAVE_GET_AFFECTED_ROWS;
			ELSIF ReceipeDBExecute.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			(* test if on row is saved *)
		eSAVE_GET_AFFECTED_ROWS :
			ReceipeDBGetAffectedRows.enable := TRUE;
			ReceipeDBGetAffectedRows.connectionIdent := ConnectionIdent;
			eStep := eSAVE_GET_AFFECTED_ROWS_r;
		eSAVE_GET_AFFECTED_ROWS_r :
			ReceipeDBGetAffectedRows();
			IF ReceipeDBGetAffectedRows.status = ERR_OK THEN
				IF ReceipeDBGetAffectedRows.affectedRows = 1 THEN	(* one row updated -> correct *)
					Command.uStatus := 0;
				ELSE
					Command.uStatus := 1;
				END_IF
				eStep := eWAIT_FOR_COMMAND;
			ELSIF ReceipeDBGetAffectedRows.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			
			(* save with new name *)
			(* only if not exist *)
		eSAVE_AS :
			brsstrcpy( ADR(sSQLstring), ADR('IF NOT EXISTS (SELECT * FROM ') );
			brsstrcat( ADR(sSQLstring), ADR(TABLE) );
			brsstrcat( ADR(sSQLstring), ADR(' WHERE name=$'') );
			brsstrcat( ADR(sSQLstring), ADR(Command.sNewReceipe) );
			brsstrcat( ADR(sSQLstring), ADR('$') ') );
			brsstrcat( ADR(sSQLstring), ADR('INSERT INTO ') );
			brsstrcat( ADR(sSQLstring), ADR(TABLE) );
			brsstrcat( ADR(sSQLstring), ADR(' (name, price, setTemp, milk, sugar, coffee, water) ') );
			brsstrcat( ADR(sSQLstring), ADR('VALUES ($'') );
			brsstrcat( ADR(sSQLstring), ADR(Command.sNewReceipe) );
			brsstrcat( ADR(sSQLstring), ADR('$',$'') );
			brsftoa( gMainLogic.par.receipe.price, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );			
			brsstrcat( ADR(sSQLstring), ADR('$',$'') );
			brsftoa( gMainLogic.par.receipe.setTemp, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );			
			brsstrcat( ADR(sSQLstring), ADR('$',$'') );
			brsftoa( gMainLogic.par.receipe.milk, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );			
			brsstrcat( ADR(sSQLstring), ADR('$',$'') );
			brsftoa( gMainLogic.par.receipe.sugar, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );			
			brsstrcat( ADR(sSQLstring), ADR('$',$'') );
			brsftoa( gMainLogic.par.receipe.coffee, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );			
			brsstrcat( ADR(sSQLstring), ADR('$',$'') );
			brsftoa( gMainLogic.par.receipe.water, ADR(sSQLstring) + brsstrlen(ADR(sSQLstring)) );			
			brsstrcat( ADR(sSQLstring), ADR('$')') );
			ReceipeDBExecute.enable := TRUE;
			ReceipeDBExecute.pSqlStatement := ADR(sSQLstring);
			ReceipeDBExecute.connectionIdent := ConnectionIdent;
			eStep := eSAVE_AS_r;
		eSAVE_AS_r :
			ReceipeDBExecute();
			IF ReceipeDBExecute.status = ERR_OK THEN
				eStep := eSAVE_AS_ROWS_AFFECTED;
			ELSIF ReceipeDBExecute.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			(* test if on row is changed *)
		eSAVE_AS_ROWS_AFFECTED :
			ReceipeDBGetAffectedRows.enable := TRUE;
			ReceipeDBGetAffectedRows.connectionIdent := ConnectionIdent;
			eStep := eSAVE_AS_ROWS_AFFECTED_r;
		eSAVE_AS_ROWS_AFFECTED_r :
			ReceipeDBGetAffectedRows();
			IF ReceipeDBGetAffectedRows.status = ERR_OK THEN
				IF ReceipeDBGetAffectedRows.affectedRows = 1 THEN	(* one row updated -> correct *)
					Command.uStatus := 0;
					brsstrcpy( ADR(Command.sActReceipe), ADR(Command.sNewReceipe) );
				ELSE
					Command.uStatus := 1;
					brsstrcpy( ADR(Command.sActReceipe), ADR('') );
				END_IF
				eStep := eGET_RECEIPE_NAMES;
			ELSIF ReceipeDBGetAffectedRows.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
							
			(* delete receipe *)
		eDELETE_RECEIPE :
			brsstrcpy( ADR(sSQLstring), ADR('DELETE FROM ') );
			brsstrcat( ADR(sSQLstring), ADR(TABLE) );
			brsstrcat( ADR(sSQLstring), ADR(' WHERE name=$'') );
			brsstrcat( ADR(sSQLstring), ADR(Command.sNewReceipe) );
			brsstrcat( ADR(sSQLstring), ADR('$'') );
			ReceipeDBExecute.enable := TRUE;
			ReceipeDBExecute.pSqlStatement := ADR(sSQLstring);
			ReceipeDBExecute.connectionIdent := ConnectionIdent;
			eStep := eDELETE_RECEIPE_r;
		eDELETE_RECEIPE_r :
			ReceipeDBExecute();
			IF ReceipeDBExecute.status = ERR_OK THEN
				eStep := eDELETE_ROWS_AFFECTED;
			ELSIF ReceipeDBExecute.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			(* test if on row is changed *)
		eDELETE_ROWS_AFFECTED :
			ReceipeDBGetAffectedRows.enable := TRUE;
			ReceipeDBGetAffectedRows.connectionIdent := ConnectionIdent;
			eStep := eDELETE_ROWS_AFFECTED_r;
		eDELETE_ROWS_AFFECTED_r :
			ReceipeDBGetAffectedRows();
			IF ReceipeDBGetAffectedRows.status = ERR_OK THEN
				IF ReceipeDBGetAffectedRows.affectedRows = 1 THEN	(* one row updated -> correct *)
					Command.uStatus := 0;
					IF brsstrcmp( ADR(Command.sActReceipe), ADR(Command.sNewReceipe) ) = 0 THEN	(* when delete actual receipe *)
						brsstrcpy( ADR(Command.sActReceipe), ADR('') );
					END_IF					
					brsstrcpy( ADR(Command.sNewReceipe), ADR('') );
				ELSE
					Command.uStatus := 1;
				END_IF
				eStep := eGET_RECEIPE_NAMES;
			ELSIF ReceipeDBGetAffectedRows.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
		
			(* rename receipe *)
			(* only if new name not exist *)
		eRENAME_RECEIPE :
			brsstrcpy( ADR(sSQLstring), ADR('IF NOT EXISTS (SELECT * FROM ') );
			brsstrcat( ADR(sSQLstring), ADR(TABLE) );
			brsstrcat( ADR(sSQLstring), ADR(' WHERE name=$'') );
			brsstrcat( ADR(sSQLstring), ADR(Command.sNewReceipe) );
			brsstrcat( ADR(sSQLstring), ADR('$') ') );
			brsstrcat( ADR(sSQLstring), ADR('UPDATE ') );
			brsstrcat( ADR(sSQLstring), ADR(TABLE) );
			brsstrcat( ADR(sSQLstring), ADR(' SET name=$'') );
			brsstrcat( ADR(sSQLstring), ADR(Command.sNewReceipe) );
			brsstrcat( ADR(sSQLstring), ADR('$' WHERE name=$'') );
			brsstrcat( ADR(sSQLstring), ADR(Command.sActReceipe) );
			brsstrcat( ADR(sSQLstring), ADR('$'') );
			ReceipeDBExecute.enable := TRUE;
			ReceipeDBExecute.pSqlStatement := ADR(sSQLstring);
			ReceipeDBExecute.connectionIdent := ConnectionIdent;
			eStep := eRENAME_RECEIPE_r;
		eRENAME_RECEIPE_r :
			ReceipeDBExecute();
			IF ReceipeDBExecute.status = ERR_OK THEN
				eStep := eRENAME_GET_AFFECTED_ROWS;
			ELSIF ReceipeDBExecute.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF
			(* test if on row is changed *)
		eRENAME_GET_AFFECTED_ROWS :
			ReceipeDBGetAffectedRows.enable := TRUE;
			ReceipeDBGetAffectedRows.connectionIdent := ConnectionIdent;
			eStep := eRENAME_GET_AFFECTED_ROWS_r;
		eRENAME_GET_AFFECTED_ROWS_r :
			ReceipeDBGetAffectedRows();
			IF ReceipeDBGetAffectedRows.status = ERR_OK THEN
				IF ReceipeDBGetAffectedRows.affectedRows = 1 THEN	(* one row updated -> correct *)
					Command.uStatus := 0;
					brsstrcpy( ADR(Command.sActReceipe),ADR(Command.sNewReceipe) );
				ELSE
					Command.uStatus := 1;
					brsstrcpy( ADR(Command.sActReceipe),ADR('') );
				END_IF
				eStep := eGET_RECEIPE_NAMES;
			ELSIF ReceipeDBGetAffectedRows.status <> ERR_FUB_BUSY THEN
				eStep := eGET_ERROR;
			END_IF

			(* disconnect from server *)
		eDISCONNECT :
			ReceipeDBDisconnect.enable := TRUE;
			ReceipeDBDisconnect.connectionIdent := ConnectionIdent;
			eStep := eDISCONNECT_r;
		eDISCONNECT_r :
			ReceipeDBDisconnect();
			IF ReceipeDBDisconnect.status <> ERR_FUB_BUSY THEN
				eStep := eSTOP;
				ConnectionIdent := 0;
			END_IF		

			(* get error string *)
		eGET_ERROR :
			brsmemset( ADR(Command.sErrorString), 0, SIZEOF(Command.sErrorString) );
			ReceipeDBErrMsg.enable := TRUE;
			ReceipeDBErrMsg.connectionIdent := ConnectionIdent;
			ReceipeDBErrMsg.pErrorMessage := ADR( Command.sErrorString );
			ReceipeDBErrMsg.errorMessageSize := SIZEOF( Command.sErrorString ) - 1;
			eStep := eGET_ERROR_r;
		eGET_ERROR_r :
			ReceipeDBErrMsg();
			
			IF ReceipeDBErrMsg.status <> ERR_FUB_BUSY THEN
				Command.uStatus := 2;
				eStep := eERROR;
			END_IF
	END_CASE;
	DBConnectState();
	
END_PROGRAM


(********************************************************************
 * COPYRIGHT --  
 ********************************************************************
 * Program: SQLRecipe
 * File: SQLRecipe_exit.st
 * Author: B&R
 * Created: January 04, 2011
 *******************************************************************)

PROGRAM _EXIT
	WHILE eStep <> eSTOP DO
		CASE eStep OF
			(* Finish dbConnect FUB befor close connection *)
			eCONNECT_r:
				ReceipeDBConnect();
				IF ReceipeDBConnect.status <> ERR_FUB_BUSY THEN
					eStep := eDISCONNECT;
				END_IF
				(* Finish dbExecuteSQL FUB *)
				eGET_TABLES_r,
				eCREATE_TABLE_r,
				eINSERT_DEFAULT_r,
				eGET_RECEIPE_NAMES_r,
				eLOAD_RECEIPE_r,
				eSAVE_RECEIPE_r,
				eSAVE_AS_r,
				eDELETE_RECEIPE_r,
			eRENAME_RECEIPE_r:
				ReceipeDBExecute();
				IF ReceipeDBExecute.status <> ERR_FUB_BUSY THEN
					eStep := eDISCONNECT;
				END_IF
				(* Finish dbGetAffectedRows FUB *)
				eSAVE_GET_AFFECTED_ROWS_r,
				eSAVE_AS_ROWS_AFFECTED_r,
				eDELETE_ROWS_AFFECTED_r,
			eRENAME_GET_AFFECTED_ROWS_r:
				ReceipeDBGetAffectedRows();
				IF ReceipeDBGetAffectedRows.status <> ERR_FUB_BUSY THEN
					eStep := eDISCONNECT;
				END_IF
				(* Finish dbFetchNextRow FUB *)
				eFETCH_NEXT_ROW_r,
				eFETCH_RECEIPE_NAME_r,
			eFETCH_VALUES_r:
				ReceipeDBFetchNextRow();
				IF ReceipeDBFetchNextRow.status <> ERR_FUB_BUSY THEN
					eStep := eDISCONNECT;
				END_IF
				(* Finish dbGetData FUB *)
				eREAD_TABLE_NAME_r,
				eREAD_RECEIPE_NAME_r,
			eGET_VALUES_r:
				ReceipeDBGetData();
				IF ReceipeDBGetData.status <> ERR_FUB_BUSY THEN
					eStep := eDISCONNECT;
				END_IF
				(* Finish dbGetErrorMessage FUB *)
			eGET_ERROR_r:
				ReceipeDBErrMsg();
				IF ReceipeDBErrMsg.status <> ERR_FUB_BUSY THEN
					eStep := eDISCONNECT;
				END_IF				
				(* Disconnect *)
			eDISCONNECT:
				ReceipeDBDisconnect.enable := TRUE;
				ReceipeDBDisconnect.connectionIdent := ConnectionIdent;
				eStep := eDISCONNECT_r;
				(* Run Disconnect FUB *)
			eDISCONNECT_r:
				ReceipeDBDisconnect();
				IF ReceipeDBDisconnect.status <> ERR_FUB_BUSY THEN
					eStep := eSTOP;
				END_IF
				
			eERROR:
				eStep := eDISCONNECT;
			ELSE
				eStep := eDISCONNECT;			
		END_CASE
	END_WHILE;
END_PROGRAM
