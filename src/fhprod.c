// Far Horizons Game Engine
// Copyright (C) 2022 Michael D Henderson
// Copyright (C) 2021 Raven Zachary
// Copyright (C) 2019 Casey Link, Adam Piggott
// Copyright (C) 1999 Richard A. Morneau
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "engine.h"
#include "enginevars.h"
#include "galaxy.h"
#include "galaxyio.h"
#include "stario.h"
#include "planetio.h"
#include "planetvars.h"
#include "speciesio.h"
#include "shipvars.h"
#include "speciesvars.h"
#include "namplavars.h"
#include "transactionio.h"
#include "logvars.h"
#include "commandvars.h"
#include "command.h"
#include "production.h"
#include "productionvars.h"
#include "intercept.h"

int main(int argc, char *argv[]) {
    int i, n, found, num_species, sp_num[MAX_SPECIES], sp_index, command, do_all_species;
    char filename[32], keyword[4];

    /* Seed random number generator. */
    last_random = time(NULL);
    n = rnd(100) + rnd(200) + rnd(300);
    for (i = 0; i < n; i++) {
        rnd(10);
    }

    /* Get commonly used data. */
    get_galaxy_data();
    get_transaction_data();
    ignore_field_distorters = TRUE;

    /* Check arguments. If an argument is -p, then do two passes.
     * In the first pass, display results and prompt the GM, allowing him to abort
     * if necessary before saving results to disk.
     * All other arguments must be species numbers.
     * If no species numbers are specified, then do all species. */
    num_species = 0;
    first_pass = FALSE;
    test_mode = FALSE;
    verbose_mode = FALSE;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            first_pass = TRUE;
        } else if (strcmp(argv[i], "-t") == 0) {
            test_mode = TRUE;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose_mode = TRUE;
        } else {
            n = atoi(argv[i]);
            if (n < 1 || n > galaxy.num_species) {
                fprintf(stderr, "\n    '%s' is not a valid argument!\n", argv[i]);
                exit(-1);
            }
            sp_num[num_species++] = n;
        }
    }

    if (num_species == 0) {
        num_species = galaxy.num_species;
        for (i = 0; i < num_species; i++) {
            sp_num[i] = i + 1;
        }
        do_all_species = TRUE;
    } else {
        do_all_species = FALSE;
    }

    /* Two passes through all orders will be done.
     * The first pass will check for errors and abort if any are found.
     * Results will be written to disk only on the second pass. */

    start_pass:

    if (first_pass) { printf("\nStarting first pass...\n\n"); }

    get_species_data();
    get_star_data();
    get_planet_data();

    /* Main loop. For each species, take appropriate action. */
    for (sp_index = 0; sp_index < num_species; sp_index++) {
        species_number = sp_num[sp_index];
        species_index = species_number - 1;

        found = data_in_memory[species_index];
        if (!found) {
            if (do_all_species) {
                if (first_pass) { printf("\n    Skipping species #%d.\n", species_number); }
                continue;
            } else {
                fprintf(stderr, "\n    Cannot get data for species #%d!\n", species_number);
                exit(-1);
            }
        }

        species = &spec_data[species_index];
        nampla_base = namp_data[species_index];
        ship_base = ship_data[species_index];

        home_planet = planet_base + (int) nampla_base->planet_index;

        /* Open orders file for this species. */
        sprintf(filename, "sp%02d.ord", species_number);
        input_file = fopen(filename, "r");
        if (input_file == NULL) {
            if (do_all_species) {
                if (first_pass) { printf("\n    No orders for species #%d.\n", species_number); }
                continue;
            } else {
                fprintf(stderr, "\n\tCannot open '%s' for reading!\n\n", filename);
                exit(-1);
            }
        }

        end_of_file = FALSE;

        just_opened_file = TRUE;    /* Tell command parser to skip mail header, if any. */
        find_start:

        /* Search for START PRODUCTION order. */
        found = FALSE;
        while (!found) {
            command = get_command();
            if (command == MESSAGE) {
                /* Skip MESSAGE text. It may contain a line that starts with "start". */
                while (TRUE) {
                    command = get_command();
                    if (command < 0) {
                        fprintf(stderr, "WARNING: Unterminated MESSAGE command in file %s!\n", filename);
                        break;
                    }
                    if (command == ZZZ) {
                        goto find_start;
                    }
                }
            }

            if (command < 0) {
                /* End of file. */
                break;
            }

            if (command != START) {
                continue;
            }

            /* Get the first three letters of the keyword and convert to upper case. */
            skip_whitespace();

            for (i = 0; i < 3; i++) {
                keyword[i] = toupper(*input_line_pointer);
                ++input_line_pointer;
            }
            keyword[3] = '\0';

            if (strcmp(keyword, "PRO") == 0) {
                found = TRUE;
            }
        }

        if (!found) {
            if (first_pass) {
                printf("\nNo production orders for species #%d, SP %s.\n", species_number, species->name);
            }
            goto done_orders;
        }

        /* Open log file. Use stdout for first pass. */
        log_stdout = FALSE;  /* We will control value of log_file from here. */
        if (first_pass) {
            log_file = stdout;
        } else {
            /* Open log file for appending. */
            sprintf(filename, "sp%02d.log", species_number);
            log_file = fopen(filename, "a");
            if (log_file == NULL) {
                fprintf(stderr, "\n\tCannot open '%s' for appending!\n\n", filename);
                exit(-1);
            }
            fprintf(log_file, "\nProduction orders:\n");
            fprintf(log_file, "\n  Number of economic units at start of production: %d\n\n", species->econ_units);
        }

        /* Initialize "done" arrays.
         * They will be used to prevent more than one corresponding order per planet. */
        if (species->num_namplas > 1000) {
            fprintf(stderr, "\n\n\tInternal error. xxx_done array overflow!/n/n");
            exit(-1);
        }

        for (i = 0; i < species->num_namplas; i++) {
            production_done[i] = FALSE;
        }

        /* Do other initializations. */
        for (i = 0; i < species->num_namplas; i++) {
            nampla = nampla_base + i;
            nampla->auto_IUs = 0;
            nampla->auto_AUs = 0;
            nampla->IUs_needed = 0;
            nampla->AUs_needed = 0;
        }

        /* Handle production orders for this species. */
        num_intercepts = 0;
        for (i = 0; i < 6; i++) {
            sp_tech_level[i] = species->tech_level[i];
        }

        do_production_orders();

        for (i = 0; i < 6; i++) {
            species->tech_level[i] = sp_tech_level[i];
        }

        for (i = 0; i < num_intercepts; i++) {
            handle_intercept(i);
        }

        data_modified[species_index] = TRUE;

        /* If this is the second pass, close the log file. */
        if (!first_pass) {
            fclose(log_file);
        }

        done_orders:

        fclose(input_file);
    }

    if (first_pass) {
        printf("\nFinal chance to abort safely!\n");

        gamemaster_abort_option();

        first_pass = FALSE;

        free_species_data();

        free(star_base);    /* In case data was modified. */
        free(planet_base);    /* In case data was modified. */

        printf("\nStarting second pass...\n\n");

        goto start_pass;
    }

    save_species_data();

    free_species_data();

    if (planet_data_modified) {
        save_planet_data();
    }

    free(planet_base);

    save_transaction_data();

    return 0;
}


