TYPE
	ReceipeType : 	STRUCT 
		price : REAL;				(*recipe price*)
		setTemp : REAL;				(*set temperature*)
		milk : REAL;				(*milk quantity*)
		sugar : REAL;				(*sugar quantity*)
		coffee : REAL;				(*coffee quantity*)
		water : REAL;				(*water quantity*)
	END_STRUCT;
	ParType : 	STRUCT 
		receipe : ReceipeType;		(*active recipe parameters*)
	END_STRUCT;
	MainLogicType : 	STRUCT 
		par : ParType;				(*parameter structure*)
	END_STRUCT;
END_TYPE
