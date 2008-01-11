#include "fastlib/fastlib.h"

#define A1 1

#define epsilon 1e-4

#define LOGCOSH 0
#define EXP 1
#define KURTOSIS 2

// n indicates number of points
// d indicates number of dimensions (number of components or variables)


namespace {


  void SaveCorrectly(const char *filename, Matrix a) {
    Matrix a_transpose;
    la::TransposeInit(a, &a_transpose);
    data::Save(filename, a_transpose);
  }
  

  void RandVector(Vector &v) {

    index_t d = v.length();
    v.SetZero();
  
    for(index_t i = 0; i+1 < d; i+=2) {
      double a = drand48();
      double b = drand48();
      double first_term = sqrt(-2 * log(a));
      double second_term = 2 * M_PI * b;
      v[i] =   first_term * cos(second_term);
      v[i+1] = first_term * sin(second_term);
    }
  
    if((d % 2) == 1) {
      v[d - 1] = sqrt(-2 * log(drand48())) * cos(2 * M_PI * drand48());
    }

    la::Scale(1/sqrt(la::Dot(v, v)), &v);

  }


  void Center(Matrix X, Matrix &X_centered) {
    Vector col_vector_sum;
    col_vector_sum.Init(X.n_rows());
    col_vector_sum.SetZero();
  
    index_t n = X.n_cols();
 
    for(index_t i = 0; i < n; i++) {
      Vector cur_col_vector;
      X.MakeColumnVector(i, &cur_col_vector);
      la::AddTo(cur_col_vector, &col_vector_sum);
    }

    la::Scale(1/(double) n, &col_vector_sum);

    X_centered.CopyValues(X);

    for(index_t i = 0; i < n; i++) {
      Vector cur_col_vector;
      X_centered.MakeColumnVector(i, &cur_col_vector);
      la::SubFrom(col_vector_sum, &cur_col_vector);
    }

  }


  void Whiten(Matrix X, Matrix &whitening, Matrix &X_whitened) {
    Matrix X_transpose, X_cov, D, E, E_times_D;
    Vector D_vector;

    la::TransposeInit(X, &X_transpose);
    la::MulInit(X, X_transpose, &X_cov);

    la::Scale(1 / (double) (X.n_cols() - 1), &X_cov);

    X_cov.PrintDebug("X_cov");

    la::EigenvectorsInit(X_cov, &D_vector, &E);
    D.InitDiagonal(D_vector);
  
    index_t d = D.n_rows();
    for(index_t i = 0; i < d; i++) {
      D.set(i, i, 1 / sqrt(D.get(i, i)));
    }

    la::MulInit(E, D, &E_times_D);
    la::MulTransBOverwrite(E_times_D, E, &whitening);
  
    la::MulOverwrite(whitening, X, &X_whitened);
  }


  void Orthogonalize(Matrix W_old, Matrix &W) {

    Matrix W_squared, W_squared_inv_sqrt;
    
    la::MulTransAInit(W, W, &W_squared);
    
    Matrix D, E, E_times_D;
    Vector D_vector;
    
    la::EigenvectorsInit(W_squared, &D_vector, &E);
    D.InitDiagonal(D_vector);
    
    index_t d = D.n_rows();
    for(index_t i = 0; i < d; i++) {
      D.set(i, i, 1 / sqrt(D.get(i, i)));
    }
    
    la::MulInit(E, D, &E_times_D);
    la::MulTransBInit(E_times_D, E, &W_squared_inv_sqrt);
	
    // note that up until this point, W == W_old
    la::MulOverwrite(W_old, W_squared_inv_sqrt, &W);
  }
  
  
  void UnivariateFastICA(Matrix X, int contrast_type, Matrix fixed_subspace,
			 index_t dim_num, double tolerance, Vector &w) {
    index_t d = X.n_rows();
    index_t n = X.n_cols();

    RandVector(w);

  
    Vector w_old;
    w_old.Init(d);
    w_old.SetZero();



    bool converged = false;

  
    for(index_t epoch = 0; !converged; epoch++) {

      printf("\nEPOCH %"LI"d\n", epoch);

      w.PrintDebug("w at beginning of epoch");

    


      Vector first_deriv_component;
      first_deriv_component.Init(d);
      first_deriv_component.SetZero();

      double second_deriv_scale_factor = 0;

      
      if(contrast_type == LOGCOSH) {

	Vector first_deriv_dots;
	first_deriv_dots.Init(n);
	
	for(index_t i = 0; i < n; i++) {
	  Vector x;
	  X.MakeColumnVector(i, &x);
	  first_deriv_dots[i] = tanh(A1 * la::Dot(w, x));
	  la::AddExpert(first_deriv_dots[i], x, &first_deriv_component);
	}
	la::Scale(1/(double) n, &first_deriv_component);
	
	
	for(index_t i = 0; i < n; i++) {
	  second_deriv_scale_factor += first_deriv_dots[i] * first_deriv_dots[i];
	}
	
	second_deriv_scale_factor *= A1 / (double) n;
	second_deriv_scale_factor -= A1;
	

      }
      else if(contrast_type == EXP) {

	Vector dots;
	Vector exp_dots;
	dots.Init(n);
	exp_dots.Init(n);

	for(index_t i = 0; i < n; i++) {
	  Vector x;
	  X.MakeColumnVector(i, &x);

	  double dot = la::Dot(w, x);
	  dots[i] = dot;
	  exp_dots[i] = exp(-dot * dot / 2);

	  la::AddExpert(dot * exp_dots[i], x, &first_deriv_component);
	}
	la::Scale(1/(double) n, &first_deriv_component);
	
	
	for(index_t i = 0; i < n; i++) {
	  second_deriv_scale_factor += exp_dots[i] * (dots[i] * dots[i] - 1);
	}
	
	second_deriv_scale_factor /= (double) n;
      }
      else if(contrast_type == KURTOSIS) {

	Vector dots_cubed;
	dots_cubed.Init(n);

	for(index_t i = 0; i < n; i++) {
	  Vector x;
	  X.MakeColumnVector(i, &x);

	  double dot = la::Dot(w, x);
	  dots_cubed[i] = pow(dot, 3);

	  la::AddExpert(dots_cubed[i], x, &first_deriv_component);
	}

	la::Scale(1 / (double) n, &first_deriv_component);

	second_deriv_scale_factor = -3;
      }
      else {
	printf("ERROR: invalid contrast function: contrast_type = %d\n",
	       contrast_type);
	exit(SUCCESS_FAIL);
      }
      
      
      la::Scale(second_deriv_scale_factor, &w);
      la::AddTo(first_deriv_component, &w);

      first_deriv_component.PrintDebug("first_deriv_component");
      printf("second_deriv_scale_factor = %f\n", second_deriv_scale_factor);



      // normalize
      la::Scale(1/sqrt(la::Dot(w, w)), &w);

      w.PrintDebug("before correction");
    
      // make orthogonal to fixed_subspace

      for(index_t i = 0; i < dim_num; i++) {
	Vector w_i;
	fixed_subspace.MakeColumnVector(i, &w_i);
      
	la::AddExpert(-la::Dot(w_i, w), w_i, &w);

      }
    
    
      // normalize
      la::Scale(1/sqrt(la::Dot(w, w)), &w);
    

      w.PrintDebug("after correction");
    
      // check for convergence

      Vector w_diff;
      la::SubInit(w_old, w, &w_diff);

      if(la::Dot(w_diff, w_diff) < epsilon) {
	converged = true;
      }
      else {
	la::AddOverwrite(w_old, w, &w_diff);
    
	if(la::Dot(w_diff, w_diff) < epsilon) {
	  converged = true;
	}
      }

      w_old.CopyValues(w);


    }

  }


  void DeflationICA(Matrix X, int contrast_type,
		    double tolerance, Matrix &fixed_subspace) {
    
    printf("Deflation ICA\n");
    
    index_t d = X.n_rows();
    
    printf("%"LI"d Components\n", d);
  
    for(index_t i = 0; i < d; i++) {
      printf("\n\nExtracting component %"LI"d\n", i);
      Vector w;
      w.Init(d);
      UnivariateFastICA(X, contrast_type, fixed_subspace, i, tolerance, w);
      Vector fixed_subspace_vector_i;
      fixed_subspace.MakeColumnVector(i, &fixed_subspace_vector_i);
      fixed_subspace_vector_i.CopyValues(w);
    }
  
  }

  
  void SymmetricICA(Matrix X, int contrast_type,
		    double tolerance, Matrix &W) {

    printf("Symmetric ICA\n");

    index_t d = X.n_rows();
    index_t n = X.n_cols();

      

    //generate random W
      
    for(index_t i = 0; i < d; i++) {
      Vector w;
      W.MakeColumnVector(i, &w);
      RandVector(w);
    }
      
      

    Matrix W_old;
    W_old.Init(d, d);
      
      
    bool converged = false;
      
    while(!converged) {
	
      // orthogonalize W via: newW = W * (W' * W) ^ -.5;

      W_old.CopyValues(W);

      // at this point, W_old == W, and W will be changed by Orthogonalize()
      Orthogonalize(W_old, W);

      
      // use Newton-Raphson to update W

      Matrix first_deriv_part;
      
      
      if(contrast_type == LOGCOSH) {
	
	
	//tanh(A1 * X' * W);
	Matrix tanh_dot_products;
	la::MulTransAInit(X, W, &tanh_dot_products);
	for(index_t i = 0; i < d; i++) {
	  for(index_t j = 0; j < n; j++) {
	    tanh_dot_products.set(j, i,
				  tanh(A1 * tanh_dot_products.get(j, i)));
	  }
	}

	la::MulInit(X, tanh_dot_products, &first_deriv_part);
	la::Scale(1 / (double) n, &first_deriv_part);

	
	// take the mean of each column of tanh_dot_products
	for(index_t i = 0; i < d; i++) {
	  Vector w;
	  W.MakeColumnVector(i, &w);

	  Vector tanh_dot_products_col_i;
	  tanh_dot_products.MakeColumnVector(i, &tanh_dot_products_col_i);

	  double second_deriv_scale_factor = 0;
	  for(index_t j = 0; j < n; j++) {
	    second_deriv_scale_factor +=
	      tanh_dot_products_col_i[j] * tanh_dot_products_col_i[j];
	  }
	  second_deriv_scale_factor =
	    (second_deriv_scale_factor * A1 / (double) n) - A1;

	  la::Scale(second_deriv_scale_factor, &w);
	}

      }
      else if(contrast_type == EXP) {

	Matrix dot_products, exp_dot_products, u_exp_dot_products;
	la::MulTransAInit(X, W, &dot_products);
	exp_dot_products.Init(n, d);
	u_exp_dot_products.Init(n, d);
	for(index_t i = 0; i < d; i++) {
	  for(index_t j = 0; j < n; j++) {
	    double dot = dot_products.get(j, i);
	    double exp_dot_product = exp(- dot * dot / 2);
	    exp_dot_products.set(j, i, exp_dot_product);
	    u_exp_dot_products.set(j, i, dot * exp_dot_product);
	  }
	}

	la::MulInit(X, u_exp_dot_products, &first_deriv_part);
	la::Scale(1 / (double) n, &first_deriv_part);


	for(index_t i = 0; i < d; i++) {
	  Vector w;
	  W.MakeColumnVector(i, &w);

	  Vector dot_products_col_i, exp_dot_products_col_i;
	  dot_products.MakeColumnVector(i, &dot_products_col_i);
	  exp_dot_products.MakeColumnVector(i, &exp_dot_products_col_i);
	  
	  double second_deriv_scale_factor = 0;
	  for(index_t j = 0; j < n; j++) {
	    double dot = dot_products_col_i[j];
	    second_deriv_scale_factor +=
	      exp_dot_products_col_i[j] * (dot * dot - 1);
	  }
	  second_deriv_scale_factor /= (double) n;

	  la::Scale(second_deriv_scale_factor, &w);
	}
      }
      else if(contrast_type == KURTOSIS) {
	
	Matrix dot_products_cubed;
	la::MulTransAInit(X, W, &dot_products_cubed);
	for(index_t i = 0; i < d; i++) {
	  for(index_t j = 0; j < n; j++) {
	    dot_products_cubed.set(j, i, pow(dot_products_cubed.get(j, i), 3));
	  }
	}


	la::MulInit(X, dot_products_cubed, &first_deriv_part);
	la::Scale(1 / (double) n, &first_deriv_part);


	la::Scale(-3, &W);

      }
      else {
	printf("ERROR: invalid contrast function: contrast_type = %d\n",
	       contrast_type);
	exit(SUCCESS_FAIL);
      }
      

      la::AddTo(first_deriv_part, &W);


      Matrix temp;
      temp.Copy(W);
      Orthogonalize(temp, W);
      
      W_old.PrintDebug("W_old");
      W.PrintDebug("W");
      
      // compare W to W_old using some method
      
      
      Matrix W_delta_cov;
      la::MulTransAInit(W, W_old, &W_delta_cov);
      
      double min_abs_diag = DBL_MAX;
      for(index_t i = 0; i < d; i++) {
	double current_diag = fabs(W_delta_cov.get(i, i));
	if(current_diag < min_abs_diag) {
	  min_abs_diag = current_diag;
	}
      }
      
      printf("min_abs_diag = %f\n", min_abs_diag);

      if(1 - min_abs_diag < epsilon) {
	converged = true;
      }
      
      
    }
    
    
  }

  void GetSamples(int max, double percentage, Vector *selected_indices) {
    
    int num_selected = 0;
    Vector rand_nums;
    rand_nums.Init(max);
    for(index_t i = 0; i < max; i++) {
      double rand_num = drand48();
      rand_nums[i] = rand_num;
      if(rand_num <= percentage) {
	num_selected++;
      }
    }
    
    selected_indices -> Init(num_selected);
    
    int j = 0;
    for(index_t i = 0; i < max; i++) {
      if(rand_nums[i] <= percentage) {
	(*selected_indices)[j] = i;
	j++;
      }
    }
  }  
  
  
  
}





int main(int argc, char *argv[]) {
  fx_init(argc, argv);

  srand48(time(0));

  const char *data = fx_param_str(NULL, "data", NULL);

  Matrix X, X_centered, whitening, X_whitened;
  data::Load(data, &X);

  index_t d = X.n_rows(); // number of dimensions
  index_t n = X.n_cols(); // number of points

  printf("d = %d, n = %d\n", d, n);


  
  X_centered.Init(d, n);
  X_whitened.Init(d, n);
  whitening.Init(d, d);
  

  printf("centering\n");
  Center(X, X_centered);
 


  printf("whitening\n");

  Whiten(X_centered, whitening, X_whitened);


  double tolerance = 1e-4;

  Matrix post_whitening_W_transpose;
  post_whitening_W_transpose.Init(d, d);
  


  //DeflationICA(X_whitened, KURTOSIS, tolerance, post_whitening_W_transpose);

  SymmetricICA(X_whitened, KURTOSIS, tolerance, post_whitening_W_transpose);
  
  Matrix post_whitening_W;
  la::TransposeInit(post_whitening_W_transpose, &post_whitening_W);




  Matrix W;
  la::MulInit(post_whitening_W, whitening, &W);


  Matrix Y;
  la::MulInit(post_whitening_W, X_whitened, &Y);
      
  //SaveCorrectly("post_whitening_W.dat", post_whitening_W);  
  //SaveCorrectly("whitening.dat", whitening);

  SaveCorrectly("W.dat", W);

  //SaveCorrectly("X_centered.dat", X_centered);
  SaveCorrectly("X_whitened.dat", X_whitened);
  SaveCorrectly("Y.dat", Y);
  

  //fx_done();

  
  
  return 0;
}
