/*
 
 This file is a part of liblsdj, a C library for managing everything
 that has to do with LSDJ, software for writing music (chiptune) with
 your gameboy. For more information, see:
 
 * https://github.com/stijnfrishert/liblsdj
 * http://www.littlesounddj.com
 
 --------------------------------------------------------------------------------
 
 MIT License
 
 Copyright (c) 2018 - 2019 Stijn Frishert
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 
 */

#include <stdlib.h>
#include <string.h>

#include "table.h"

typedef struct lsdj_table_t
{
    // The volume column of the table
    unsigned char volumes[LSDJ_TABLE_LENGTH];
    
    // The transposition column of the table
    unsigned char transpositions[LSDJ_TABLE_LENGTH];
    
    // The first effect command column of the table
    lsdj_command_t commands1[LSDJ_TABLE_LENGTH];
    
    // The second effect command column of the table
    lsdj_command_t commands2[LSDJ_TABLE_LENGTH];
} lsdj_table_t;

lsdj_table_t* lsdj_table_new()
{
    lsdj_table_t* table = (lsdj_table_t*)malloc(sizeof(lsdj_table_t));
    lsdj_clear_table(table);
    return table;
}

lsdj_table_t* lsdj_copy_table(const lsdj_table_t* table)
{
    lsdj_table_t* newTable = malloc(sizeof(lsdj_table_t));
    memcpy(newTable, table, sizeof(lsdj_table_t));
    return newTable;
}

void lsdj_table_free(lsdj_table_t* table)
{
    free(table);
}

void lsdj_clear_table(lsdj_table_t* table)
{
    memset(table->volumes, 0, LSDJ_TABLE_LENGTH);
    memset(table->transpositions, 0, LSDJ_TABLE_LENGTH);
    
    for (int i = 0; i < LSDJ_TABLE_LENGTH; ++i)
    {
        lsdj_command_clear(&table->commands1[i]);
        lsdj_command_clear(&table->commands2[i]);
    }
}

void lsdj_table_set_volume(lsdj_table_t* table, size_t index, unsigned char volume)
{
    table->volumes[index] = volume;
}

void lsdj_table_set_volumes(lsdj_table_t* table, unsigned char* volumes)
{
    memcpy(table->volumes, volumes, sizeof(table->volumes));
}

unsigned char lsdj_table_get_volume(const lsdj_table_t* table, size_t index)
{
    return table->volumes[index];
}

void lsdj_table_set_transposition(lsdj_table_t* table, size_t index, unsigned char transposition)
{
    table->transpositions[index] = transposition;
}

void lsdj_table_set_transpositions(lsdj_table_t* table, unsigned char* transpositions)
{
    memcpy(table->transpositions, transpositions, sizeof(table->transpositions));
}

unsigned char lsdj_table_get_transposition(const lsdj_table_t* table, size_t index)
{
    return table->transpositions[index];
}

lsdj_command_t* lsdj_table_get_command1(lsdj_table_t* table, size_t index)
{
    return &table->commands1[index];
}

lsdj_command_t* lsdj_table_get_command2(lsdj_table_t* table, size_t index)
{
    return &table->commands2[index];
}
