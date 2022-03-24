/*
## Copyright (C) <2017>  <bioMerieux, Universite Claude Bernard Lyon 1,
## Centre National de la Recherche Scientifique>

## 1. This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU Affero General Public License as published
## by the Free Software Foundation version 3 of the  License and under the
## terms of article 2 below.
## 2. This program is distributed in the hope that it will be useful, but
## WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
## or FITNESS FOR A PARTICULAR PURPOSE. See below the GNU Affero General
## Public License for more details.
## You should have received a copy of the GNU Affero General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.
## 3. Communication to the public by any means, in particular in the form of
## a scientific paper, a poster, a slideshow, an internet page, or a patent,
## of a result obtained directly or indirectly by running this program must
## cite the following paper :
##  Magali Jaillard, Maud Tournoud, Leandro Lima, Vincent Lacroix,
##  Jean-Baptiste Veyrieras and Laurent Jacob, "Representing Genetic
##  Determinants in Bacterial GWAS with Compacted De Bruijn Graphs", 2017,
##  Cold Spring Harbor Labs Journals, doi:10.1101/113563.
##  (url: http://www.biorxiv.org/content/early/2017/03/03/113563)
## -------------------------------------------------------------------------

## Authors (alphabetically): Jacob L., Jaillard M., Lima L.
*/

#include "global.h"
#include "map_reads.hpp"
#include "Utils.h"
#include "PhenoCounter.h"
#include <boost/algorithm/string/predicate.hpp>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <vector>
#include <set>
#include <map>

/*********************************************************************
** METHOD  :
** PURPOSE :
** INPUT   :
** OUTPUT  :
** RETURN  :
** REMARKS :
*********************************************************************/
// The Tool constructor allows to give a name to our tool.
// This name appears when one gets help in the command line or in the final output
map_reads::map_reads ()  : Tool ("map_reads") //give a name to our tool
{
    populateParser(this);
}



/*********************************************************************
** METHOD  :
** PURPOSE : executes map_reads
** INPUT   : output folder name, unitigs
** OUTPUT  : bugwas_input.* files (4) and weight_correction file
** RETURN  :
** REMARKS : deletes graph, NodeIdToUnitigs obects as well as the tmpFolder
*********************************************************************/
void map_reads::execute ()
{
    //populates strains file if given
    checkParametersMapping(this);

    //get the parameters
    string referenceOutputFolder = getInput()->getStr(STR_OUTPUT); //keep the original output folder in a variable for file manipulation
    string outputFolder = stripLastSlashIfExists(getInput()->getStr(STR_OUTPUT))+string("/step1");
    // measuring time
    auto t1 = std::chrono::high_resolution_clock::now();

    // generate the bugwas input
    std::cout << "[Generating gemma/bugwas input files ...]" << std::endl;

    // create the ID and Phenotype file
    Strain::createIdPhenoFile(outputFolder+string("/bugwas_input.id_phenotype"), strains);
    // create the vector for the Pheno files: //TODO: si utile
    int nbUnitigs = getNbLinesInFile(referenceOutputFolder + string("/graph.nodes")); //TODO: inutile ?
    std::vector< PhenoCounter > unitigs2PhenoCounter(nbUnitigs);

    std::string matrix = referenceOutputFolder + "matrix/reindeer_output/query_results";//TODO: replace with filename; //input abundance matrix
    std::string output = "bugwas_input.all_rows.binary" ;//TODO:same ; //output presence/absence matrix
    // streams
    std::ifstream stream (matrix, std::ifstream::binary);
    std::ofstream outstream (outputFolder + output, std::ofstream::binary);
    std::ofstream weight_corr_track ("weight_correction", std::ofstream::binary);
    // other files names prefix
    size_t lastindex = output.find_last_of(".");
    std::string rawname = output.substr(0, lastindex);
    // must use stream.fail() with a switch case to now if the thing went fine : no
    // file operation causes C++ to stop.
    // see https://www.eecs.umich.edu/courses/eecs380/HANDOUTS/cppBinaryFileIO-2.html
    if (stream.fail()) {
        perror("ERROR: could not open the reindeer matrix file");
        std::exit(1);
    } else if (outstream.fail()) {
        perror("ERROR: could not open bugwas_input.all_rows.binary");
        std::exit(1);
    } else if (weight_corr_track.fail()) {
        perror("ERROR: could not open weight_correction");
        std::exit(1);
    }

    // variables
    std::vector<SKmer> vector_of_kmers;
    std::vector<std::vector<int>> vector_of_unique_patterns ;
    std::vector<std::string> filenames ;
    std::vector<std::vector<int>> unique_to_all ;
    std::map<std::vector<int>, std::vector<int>> map_unique_to_all ; //each time a unique is accountered, insert the pattern as a key and the n in the vector of values
    // reads the input data
    outstream << "ps ";
    // read the data line by line:
    std::string line_buffer;
    std::set<std::vector<int>> vector_set;
    int n = 0; // line counter
    while(std::getline(stream, line_buffer).good()) {
        if (line_buffer.starts_with("query")) {
            // we obtain the file names and store them:
            std::istringstream input(line_buffer);
            for (std::string word; std::getline(input, word, '\t'); ) {
                filenames.push_back(word);
            }
            // removes "query"
            filenames.erase(filenames.begin());
            // write the header of both all_rows and all_rows_unique :
            for (const std::string &i : filenames) {
                outstream << i << " ";
            }
            outstream << "\n" ;
        } else {
            // we write the body:
            // 1: parse the line and build the SKmer
            SKmer raw_data = process_line(line_buffer);
            // 2: adds the counts to thew PhenoCounter vector
            //TODO: verifier code
            for (int i =0; i < raw_data.pattern.size(); i++) {
                unitigs2PhenoCounter[n].add((*strains)[i].phenotype, raw_data.pattern.at(i)); // TODO: Strains are probably not in the same order, dammit
            }
            // 2: changes abundance counts to presence/absence (0 stays 0 and more than 1 becomes 1)
            SKmer binarised_data = binarise_counts(raw_data);
            // 3: change, if needed, the allele description of the SKmer
            SKmer data = minor_allele_description(binarised_data);
            // 4: keep track of the change in allele description
            weight_corr_track << data.corrected << "\n";
            // next
            vector_of_kmers.push_back(data);
            outstream << n << " " ;
            for (const auto &i : data.pattern) {
                outstream << i << " " ;
            }
            outstream << "\n" ;
            // looks for the vector in the set of unique vectors :
            auto search = vector_set.find(data.pattern) ;
            // if not found, adds it to the vector of unique vectors
            if (search == vector_set.end()) {
                vector_of_unique_patterns.push_back(data.pattern);
                vector_set.emplace(data.pattern);
                std::vector<int> unitigs{n};
                map_unique_to_all.emplace(data.pattern, unitigs);
            } else {
                map_unique_to_all[data.pattern].push_back(n);
                // add n to the item (vector) pointed out above
            }
            n++;
        }
    }
    stream.close();
    outstream.close();
    weight_corr_track.close();

    // writes uniques and unique_to_all, gemma unique patterns to nb unitigs outputs
    write_bugwas_gemma(vector_of_unique_patterns, rawname, filenames, map_unique_to_all);

    // create a vector indexed by the unitigIndex containing each position a vector of phenotypeValue,
    // indicating the phenotypes of each appearance of the unitig in the strains
    // it can be used to know the total count of a unitig (size of the vector) and their phenotype count in step 3
    // (e.g. how many times an unitig appeared in strains with phenotype 0, >0 and NA)
    //TODO: instead of representing the phenotype of each appearance, just use a pair <count, phenotype>
    //TODO: this could save disk
    cerr << "[Generating unitigs2PhenoCounter...]" << endl;
    //get the nbContigs
/*    int nbUnitigs = getNbLinesInFile(referenceOutputFolder + string("/graph.nodes")); //TODO:inutile
    vector< PhenoCounter > unitigs2PhenoCounter(nbUnitigs); //TODO: a mettre au debut de mon traitement des lignes
    for(int strainIndex=0;strainIndex<allReadFilesNames.size();strainIndex++) {
        for (int unitigIndex=0; unitigIndex<nbUnitigs; unitigIndex++) { //TODO: remplacer ca par le fait qu'on parcours deja tous les untitigs, donc index devient juste l'ordre de traitement des lignes
            int count;
            unitigs2PhenoCounter[unitigIndex].add((*strains)[strainIndex].phenotype, count);
        }
    }*/

    //serialize unitigs2PhenoCounter
    {
        ofstream unitigs2PhenoCounterFile;
        openFileForWriting(outputFolder+string("/unitigs2PhenoCounter"), unitigs2PhenoCounterFile);
        boost::archive::text_oarchive boostOutputArchive(unitigs2PhenoCounterFile);
        //serialization itself
        boostOutputArchive & unitigs2PhenoCounter;
    } //boostOutputArchive and the stream are closed on destruction

    Strain::createPhenotypeCounter(outputFolder+string("/phenoCounter"), strains); //TODO: devrait marcher
    cerr << "[Generating unitigs2PhenoCounter...] - Done!" << endl;

    // finishing measuring time
    auto t2 = std::chrono::high_resolution_clock::now();
    auto ms_int = std::chrono::duration_cast<std::chrono::minutes>(t2 - t1);
    std::cout << "Conversion took " << ms_int.count() << "min\n";

    //after the mapping, free some memory that will not be needed anymore
    delete graph;
    delete nodeIdToUnitigId;

    //clean-up - saving some disk space
    //remove GATB's graph file
    remove((referenceOutputFolder+string("/graph.h5")).c_str());

    cerr << endl << "[Generating gemma/bugwas input files ...] - Done!" << endl;
    cerr.flush();
}

SKmer process_line(const std::string& line_buffer) {
    /*
     * this function processes lines by putting them in a structure than contains  the Kmer name, a vector of its abundance pattern. Ignores the corrected attribute.
     */
    std::vector<int> output_pattern;
    std::string kmer_name;
    std::istringstream input(line_buffer);
    //loop over tab-separated words in the line :
    for (std::string word; std::getline(input, word, '\t'); ) {
        if (word.starts_with(">")) { // if id resets line
            kmer_name = word;
        } else if (word.ends_with("*")) { // if abundance is 0
            output_pattern.push_back(0);
        } else { // if abundance is more than 0
            size_t lastindex = word.find_last_of(":");
            std::string appearance_nb = word.substr(lastindex,word.size()-lastindex); // finds 17 in 0-20:17
            output_pattern.push_back(std::stoi(appearance_nb));
        }
    }
    // Kmer output_struct{kmer_name, output_pattern};
    // output_pattern.clear();
    return {kmer_name, output_pattern};
}

void unitigs_to_Phenocounter(SKmer& data, std::vector<int>& vector_pheno_count) {
    /*
     * This function adds a Kmer abundance to Phenocounter vector (serialised). Vector follows the order : 0, 1, NA
     */


}

SKmer binarise_counts(SKmer& data) {
    /*
     * this function binaries the abundance of a kmer
     */
    for (int i = 0; i < data.pattern.size(); i++) {
        switch (data.pattern.at(i)) {
            case 0:
                break;
            default:
                data.pattern.at(i) = 1;
                break;
        }
    }
    return data;
}

SKmer minor_allele_description(SKmer& data) {
    /*
     * this function changes the pattern of presence/absence of a SKmer into the minor allele description if needed, and changed the 'corrected' accordingly (1: did not change; -1: changed).
     */
    float sum ;
    std::vector<int> corr_vector;
    for (auto& n : data.pattern) {
        sum += n;
    }
    if (sum/(float)data.pattern.size() > 0.5) {
        for (int i = 0; i < data.pattern.size(); i++) {
            switch (data.pattern.at(i)) {
                case 0:
                    corr_vector.push_back(1);
                    break;
                case 1:
                    corr_vector.push_back(0);
                    break;
            }
        }
        data.corrected = -1;
        data.pattern = corr_vector;
    } else {
        data.corrected = 1;
    }
    return data;
}


void write_bugwas_gemma(const std::vector<std::vector<int>>& vector_of_unique_patterns, std::string& rawname, std::vector<std::string>& filenames, std::map<std::vector<int>, std::vector<int>>& map_unique_to_all) {
    /*
     * this function builds output files : unique_patterns, unique_to_all, and gemma_pattern_to_nb_unitigs, gemma_unitig_to_patterns.
     */
    std::ofstream outstream_unique (rawname+".unique_rows.binary", std::ofstream::binary);
    std::ofstream outstream_unique_to_all (rawname+".unique_rows_to_all_rows.binary", std::ofstream::binary);
    std::ofstream outstream_gemma_pattern_to_nb_unitigs ("gemma_input.pattern_to_nb_of_unitigs.binary", std::ofstream::binary);
    std::ofstream outstream_gemma_unitig_to_patterns ("gemma_input.unitig_to_pattern.binary", std::ofstream::binary);


    //error check
    if (outstream_unique.fail()) {
        perror("ERROR: could not open bugwas_input.unique_rows.binary");
        std::exit(1);
    } else if (outstream_unique_to_all.fail()) {
        perror("ERROR: could not open bugwas_inputunique_rows_to_all_rows.binary");
        std::exit(1);
    } else if (outstream_gemma_pattern_to_nb_unitigs.fail()) {
        perror("ERROR: could not open gemma_input.pattern_to_nb_of_unitigs.binary");
        std::exit(1);
    } else if (outstream_gemma_unitig_to_patterns.fail()) {
        perror("ERROR: could not open gemma_input.unitig_to_pattern.binary");
        std::exit(1);
    }

    //TODO: check that my understanding of the output files is ok

    // header for unique_pattern
    outstream_unique << "ps ";
    for (const std::string &i : filenames) {
        outstream_unique << i << " ";
    }
    outstream_unique << "\n";

    int n = 0;
    for (const auto &i : vector_of_unique_patterns) {
        // writes the unique patterns in their file
        outstream_unique << n << " ";
        for (const auto &j : i) {
            outstream_unique << j << " " ;
        }
        outstream_unique << "\n" ;
        // writes connection between unique pattern and all the unitigs each represents in unique_to_all, and the reciprocal in unitig_to_pattern
        for (const auto &j : map_unique_to_all[i]) {
            outstream_unique_to_all << j << " ";
            outstream_gemma_unitig_to_patterns << j << " " << n << "\n";
        }
        outstream_unique_to_all << "\n" ;

        // writes the number of unitigs that each unique pattern represents
        outstream_gemma_pattern_to_nb_unitigs << n << " " << map_unique_to_all[i].size() << "\n";

        n++;
    }
    outstream_unique.close();
    outstream_unique_to_all.close();
    outstream_gemma_pattern_to_nb_unitigs.close();
    outstream_gemma_unitig_to_patterns.close();
}
