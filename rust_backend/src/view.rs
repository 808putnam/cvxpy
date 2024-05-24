use crate::backend::CONST_ID;
use crate::tensor_representation::TensorRepresentation;
use crate::{
    faer_ext::{self, to_triplets_iter},
    IdxMap,
};
use faer::sparse::SparseColMat;
use pyo3::prelude::*;
use std::collections::HashMap;

#[derive(Default)]
pub(crate) struct ViewContext {
    /// Maps variable id to first column associated with its entries
    pub(crate) id_to_col: IdxMap, 
    /// Maps parameter id to number of entries in parameter
    pub(crate) param_to_size: IdxMap, 
    /// Maps parameter id to first matrix/slice (column in a 3D
    /// sense) associated with its entries
    pub(crate) param_to_col: IdxMap,
    /// Total number of parameter entries + 1
    pub(crate) param_size_plus_one: i64,
    /// Total number of variables in problem
    pub(crate) var_length: i64, 
}
type VarId = i64;
type ParamId = i64;

pub(crate) type Tensor = HashMap<VarId, HashMap<ParamId, crate::SparseMatrix>>;

pub(crate) struct View<'a> {
    pub(crate) variables: Vec<i64>, // todo: turn into a set
    pub(crate) tensor: Tensor,
    pub(crate) is_parameter_free: bool,
    pub(crate) context: &'a ViewContext,
}

impl<'a> View<'a> {
    pub fn new(context: &'a ViewContext) -> Self {
        View {
            variables: Vec::new(),
            tensor: Tensor::new(),
            is_parameter_free: true,
            context,
        }
    }

    pub fn get_tensor_representation(&self, row_offset: i64) -> TensorRepresentation {
        let mut tensor_representations = Vec::new();

        for (&variable_id, variable_tensor) in self.tensor.iter() {
            for (&parameter_id, parameter_matrix) in variable_tensor.iter() {
                let p = self.context.param_to_size[&parameter_id];
                let m = parameter_matrix.nrows() as i64 / p;
                let (new_rows, new_cols, data, new_param_offset): (
                    Vec<u64>,
                    Vec<u64>,
                    Vec<f64>,
                    Vec<u64>,
                ) = to_triplets_iter(parameter_matrix)
                    .map(|(i, j, d)| {
                        let row_index = (i as i64 % m + row_offset) as u64;
                        let col_index = (j as i64 + self.context.id_to_col[&variable_id]) as u64;
                        let param_offset =
                            (i as i64 / m + self.context.param_to_col[&parameter_id]) as u64;
                        (row_index, col_index, d, param_offset)
                    })
                    .fold(
                        (Vec::new(), Vec::new(), Vec::new(), Vec::new()),
                        |(mut rows, mut cols, mut data, mut param_offset), (row, col, d, param)| {
                            rows.push(row);
                            cols.push(col);
                            data.push(d);
                            param_offset.push(param);
                            (rows, cols, data, param_offset)
                        },
                    );

                // Add to tensor_representations
                tensor_representations.push(TensorRepresentation {
                    data,
                    row: new_rows,
                    col: new_cols,
                    parameter_offset: new_param_offset,
                });
            }
        }

        TensorRepresentation::combine(tensor_representations)
    }

    pub fn apply_all<F>(&mut self, mut func: F)
    where
        F: FnMut(&crate::SparseMatrix, i64) -> crate::SparseMatrix,
    {
        self.tensor = self
            .tensor
            .iter()
            .map(|(var_id, parameter_repr)| {
                (
                    *var_id,
                    parameter_repr
                        .iter()
                        .map(|(k, v)| (*k, func(v, self.context.param_to_size[k])))
                        .collect(),
                )
            })
            .collect();
    }

    pub(crate) fn select_rows(&mut self, rows: &[u64]) {
        let func = |x: &SparseColMat<u64, f64>, p: i64| -> crate::SparseMatrix {
            if p == 1 {
                faer_ext::select_rows(x, rows)
            } else {
                let m = (x.nrows() / p as usize) as u64;
                let mut new_rows = Vec::with_capacity(rows.len() * p as usize);
                for i in 0..p as u64 {
                    for &r in rows {
                        new_rows.push(r + m * i);
                    }
                }
                faer_ext::select_rows(x, rows)
            }
        };

        self.apply_all(func);
    }

    pub(crate) fn rows(&self) -> u64 {
        for (_, tensor) in &self.tensor {
            for (param_id, param_mat) in tensor {
                return param_mat.nrows() as u64 / self.context.param_to_size[&param_id] as u64;
            }
            panic!("No parameters in tensor");
        }
        panic!("No variables in tensor");
    }

    pub(crate) fn accumulate_over_variables(
        mut self,
        func: impl Fn(&SparseColMat<u64, f64>, u64) -> SparseColMat<u64, f64>,
        is_parameter_free_function: bool,
    ) -> Self {
        for (variable_id, tensor) in &self.tensor {
            self.tensor[variable_id] = if is_parameter_free_function {
                self.apply_to_parameters(func, tensor)
            } else {
                // func(&tensor[&CONST_ID], 1)
                todo!("Implement accumulate_over_variables")
            };
        }

        let is_parameter_free = self.is_parameter_free && is_parameter_free_function;
        self
    }

    pub(crate) fn apply_to_parameters(
        &self,
        func: impl Fn(&SparseColMat<u64, f64>, u64) -> SparseColMat<u64, f64>,
        tensor: &HashMap<i64, SparseColMat<u64, f64>>,
    ) -> HashMap<i64, SparseColMat<u64, f64>> {
        todo!("Implement apply_to_parameters")
    }
}