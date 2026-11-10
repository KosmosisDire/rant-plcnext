TYPE
    Phase : (Idle := 0, Active := 1, Finished := 2);
    Inner : STRUCT
        a : INT;
        b : REAL;
    END_STRUCT;
    Sample : STRUCT
        id : DINT;
        label : STRING[15];
        inner : Inner;
        grid : ARRAY[0..1, 0..2] OF INT;
        phase : Phase;
        span : TIME;
    END_STRUCT;
END_TYPE
