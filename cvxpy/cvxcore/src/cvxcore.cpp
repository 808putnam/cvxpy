//   Copyright 2017 Steven Diamond
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#include "cvxcore.hpp"
#include <iostream>
#include <map>
#include "LinOp.hpp"
#include "LinOpOperations.hpp"
#include "ProblemData.hpp"
#include "Utils.hpp"


/* function: add_matrix_to_vectors
*
* This function adds a matrix to our sparse matrix triplet
* representation, by using eigen's sparse matrix iterator
* This function takes horizontal and vertical offset, which indicate
* the offset of this block within our larger matrix.
*/
void add_matrix_to_vectors(Matrix &block, std::vector<double> &V,
                           std::vector<int>  &I, std::vector<int> &J,
                           int &vert_offset, int &horiz_offset){
	for ( int k = 0; k < block.outerSize(); ++k ) {
		for ( Matrix::InnerIterator it(block, k); it; ++it ){
			V.push_back(it.value());

			/* Push back current row and column indices */
			I.push_back(it.row() + vert_offset);
			J.push_back(it.col() + horiz_offset);
		}
	}
}

void extend_constant_vec(std::vector<double> &const_vec, int &vert_offset,
                         Matrix &block){
	int rows = block.rows();
	for ( int k = 0; k < block.outerSize(); ++k ){
		for ( Matrix::InnerIterator it(block, k); it; ++it ){
			int idx = vert_offset + (it.col() * rows) + it.row();
			const_vec[idx] += it.value();
		}
	}
}


void process_constraint(LinOp & lin, ProblemTensor & problemData,
                        int &vert_offset, int var_length,
                        std::map<int, int> &id_to_col){
	/* Get the coefficient for the current constraint */
	Tensor coeffs = lin_to_tensor(lin, 0);
  std::cout << "Tensor coeffs = lin_to_tensor(lin, 0);\n";

	typedef Tensor::iterator it_type;
	for(it_type it = coeffs.begin(); it != coeffs.end(); ++it){
		int param_id = it->first;
    std::vector<ProblemData> probVec;
    problemData[param_id] = probVec;
    DictMat var_map = it->second;
    typedef DictMat::iterator inner_it_type;
    for(inner_it_type in_it = var_map.begin(); in_it != var_map.end(); ++in_it) {
      int var_id = in_it->first;				// Horiz offset determined by the id
      std::vector<Matrix> blocks = in_it->second;
      // Constant term is last column.
      for (unsigned i=0; i < blocks.size(); i++) {
        int horiz_offset;
        if (var_id == CONSTANT_ID) { // Add to CONSTANT_VEC if linop is constant
          horiz_offset = var_length;
        } else {
          horiz_offset = id_to_col[var_id];
        }
        ProblemData probBlock;
        probVec.push_back(probBlock);
        std::cout << param_id << "\n";
        std::cout << var_id << "\n";
        std::cout << i << "\n";
        add_matrix_to_vectors(blocks[i],
                              probBlock.V,
                              probBlock.I,
                              probBlock.J,
                              vert_offset,
                              horiz_offset);
        std::cout << "add_to_matrix" << "\n";
      }
    }
	}
}

/* Returns the number of rows in the matrix assuming vertical stacking
	 of coefficient matrices */
int get_total_constraint_length(std::vector< LinOp* > constraints){
	int result = 0;
	for (unsigned i = 0; i < constraints.size(); i++) {
		result += vecprod(constraints[i]->size);
	}
	return result;
}

/* Returns the number of rows in the matrix using the user provided vertical
	 offsets for each constraint. */
int get_total_constraint_length(std::vector<LinOp*> &constraints,
                                std::vector<int> &constr_offsets){
	/* Must specify an offset for each constraint */
	if(constraints.size() != constr_offsets.size()){
		std::cerr << "Error: Invalid constraint offsets: ";
		std::cerr	<< "CONSTR_OFFSET must be the same length as CONSTRAINTS" << std::endl;
		exit(-1);
	}

	int offset_end = 0;
	/* Offsets must be monotonically increasing */
	for(unsigned i = 0; i < constr_offsets.size(); i++){
		LinOp constr = *constraints[i];
		int offset_start = constr_offsets[i];
    offset_end = offset_start + vecprod(constr.size);

		if(i + 1 < constr_offsets.size() && constr_offsets[i + 1] < offset_end){
			std::cerr << "Error: Invalid constraint offsets: ";
			std::cerr << "Offsets are not monotonically increasing" << std::endl;
			exit(-1);
		}
	}
	return offset_end;
}

// Create a tensor with a problem data entry for each parameter,
// as a vector with entries equal to the parameter size.
ProblemTensor init_data_tensor(std::map<int, int> param_to_size) {
  ProblemTensor output;
	typedef std::map<int, int>::iterator it_type;
	for (it_type it = param_to_size.begin();
       it != param_to_size.end();
       ++it) {
    int param_id = it->first;
    int param_size = it->second;
    std::vector<ProblemData> data_block(param_size);
    output[param_id] = data_block;
  }
  return output;
}

/* function: build_matrix
*
* Description: Given a list of linear operations, this function returns a data
* structure containing a sparse matrix representation of the cone program.
*
* Input: std::vector<LinOp *> constraints, our list of constraints represented
* as a linear operation tree
*
* Output: prob_data, a data structure which contains a sparse representation
* of the coefficient matrix, a dense representation of the constant vector,
* and maps containing our mapping from variables, and a map from the rows of our
* matrix to their corresponding constraint.
*
*/
ProblemTensor build_matrix(std::vector< LinOp* > constraints,
                           int var_length,
                           std::map<int, int> id_to_col,
                           std::map<int, int> param_to_size) {
  ProblemTensor prob_data = init_data_tensor(param_to_size);
	int num_rows = get_total_constraint_length(constraints);
  std::cout << "tell2\n";
	int vert_offset = 0;
	/* Build matrix one constraint at a time */
	for (unsigned i = 0; i < constraints.size(); i++){
		LinOp constr = *constraints[i];
		process_constraint(constr, prob_data,
		                   vert_offset, var_length,
		                   id_to_col);
		vert_offset += vecprod(constr.size);
	}
	return prob_data;
}

/*  See comment above for build_matrix. Requires specification of a vertical
		offset, VERT_OFFSET, for each constraint in the vector CONSTR_OFFSETS.

		Valid CONSTR_OFFSETS assume that a vertical offset is provided for each
		constraint and that the offsets are not overlapping. In particular,
		the vertical offset for constraint i + the size of constraint i must be
		less than the vertical offset for constraint i+1.
		*/
ProblemTensor build_matrix(std::vector<LinOp*> constraints,
                           int var_length,
                           std::map<int, int> id_to_col,
                           std::map<int, int> param_to_size,
                           std::vector<int> constr_offsets){
  ProblemTensor prob_data = init_data_tensor(param_to_size);

	/* Function also verifies the offsets are valid */
	int num_rows = get_total_constraint_length(constraints, constr_offsets);

	/* Build matrix one constraint at a time */
	for (unsigned i = 0; i < constraints.size(); i++){
		LinOp constr = *constraints[i];
		int vert_offset = constr_offsets[i];
		process_constraint(constr, prob_data,
		                   vert_offset, var_length,
		                   id_to_col);
	}
	return prob_data;
}