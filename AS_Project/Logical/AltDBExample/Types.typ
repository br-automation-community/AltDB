(********************************************************************
 * COPYRIGHT --  
 ********************************************************************
 * Program: SQLRecipe
 * File: SQLRecipe.typ
 * Author: B&R
 * Created: December 27, 2010
 ********************************************************************
 * Local data types of program SQLRecipe
 ********************************************************************)

TYPE
	Steps_enum : 
		(
		eSTOP, (*Stop - nothing to do*)
		eCONNECT, (*Connect to Database*)
		eCONNECT_r,
		eGET_TABLES, (*SELECT Names of Tables in Database*)
		eGET_TABLES_r,
		eFETCH_NEXT_ROW, (*Read line by line*)
		eFETCH_NEXT_ROW_r,
		eREAD_TABLE_NAME, (*Get name out of line*)
		eREAD_TABLE_NAME_r,
		eCREATE_TABLE, (*CREATE TABLE with name if not exist*)
		eCREATE_TABLE_r,
		eINSERT_DEFAULT, (*INSERT default receipe*)
		eINSERT_DEFAULT_r,
		eGET_RECEIPE_NAMES := 30, (*SELECT Names of all receipes*)
		eGET_RECEIPE_NAMES_r,
		eFETCH_RECEIPE_NAME, (*read line by line*)
		eFETCH_RECEIPE_NAME_r,
		eREAD_RECEIPE_NAME, (*get name of receipe*)
		eREAD_RECEIPE_NAME_r,
		eWAIT_FOR_COMMAND := 50, (*wait for command of user*)
		eLOAD_RECEIPE, (*SELECT a specific receipe*)
		eLOAD_RECEIPE_r,
		eFETCH_VALUES, (*prepare for read receipe values*)
		eFETCH_VALUES_r,
		eGET_VALUES, (*read each value of receipe*)
		eGET_VALUES_r,
		eSAVE_RECEIPE, (*UPDATE changed values into receipe*)
		eSAVE_RECEIPE_r,
		eSAVE_GET_AFFECTED_ROWS, (*test if saved (if one row is modified)*)
		eSAVE_GET_AFFECTED_ROWS_r,
		eSAVE_AS, (*INSERT changed values into new receipe*)
		eSAVE_AS_r,
		eSAVE_AS_ROWS_AFFECTED, (*test if saved (if one row is modified)*)
		eSAVE_AS_ROWS_AFFECTED_r,
		eDELETE_RECEIPE, (*DELETE receipe*)
		eDELETE_RECEIPE_r,
		eDELETE_ROWS_AFFECTED, (*test if deleted (if one row is modified)*)
		eDELETE_ROWS_AFFECTED_r,
		eRENAME_RECEIPE, (*UPDATE name of receipe*)
		eRENAME_RECEIPE_r,
		eRENAME_GET_AFFECTED_ROWS, (*test if renamed (if one row is modified)*)
		eRENAME_GET_AFFECTED_ROWS_r,
		eDISCONNECT := 150, (*disconnect from server*)
		eDISCONNECT_r,
		eGET_ERROR := 200, (*receive error of server*)
		eGET_ERROR_r,
		eERROR, (*error step*)
		eDUMMY (*dummy step - for development*)
	);
END_TYPE

(*Commands for receipe handling*)

TYPE
	Command_enum : 
		(
		eCMD_IDLE, (*nothing to do*)
		eCMD_CONNECT := 10, (*connect to server*)
		eCMD_CONNECT_w,
		eCMD_LOAD := 20, (*load receipe 'sNewReceipe'*)
		eCMD_LOAD_w,
		eCMD_SAVE := 30, (*save receipe 'sActReceipe'*)
		eCMD_SAVE_w,
		eCMD_SAVEAS := 40, (*save as new receipe 'sNewReceipe'*)
		eCMD_SAVEAS_w,
		eCMD_DELETE := 50, (*delete receipe 'sNewReceipe'*)
		eCMD_DELETE_w,
		eCMD_RENAME := 60, (*rename receipe from 'sActReceipe' to 'sNewReceipe'*)
		eCMD_RENAME_w,
		eCMD_DISCONNECT := 70, (*disconnect from server*)
		eCMD_DISCONNECT_w
	);
	CommandInterface_typ : 	STRUCT 
		eCommand : Command_enum;
		sActReceipe : STRING[LENGTH_NAME];
		sNewReceipe : STRING[LENGTH_NAME];
		sReceipeNames : ARRAY[0..9] OF STRING[LENGTH_NAME];
		uStatus : UINT; (*0 = OK; 1 = FAIL SQL QUERY; 2 = Unexpected Error*)
		sErrorString : STRING[100];
	END_STRUCT;
END_TYPE
