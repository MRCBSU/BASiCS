#ifndef MCMCREGNOSPIKES_H
#define MCMCREGNOSPIKES_H

#include "utils.h"

/* MCMC sampler for regression and non-spikes case
 * N: Total number of MCMC draws 
 * Thin: Thinning period for MCMC chain 
 * Burn: Burning period for MCMC chain 
 * Counts: Matrix of expression counts
 * BatchDesign: Design matrix representing batch information
 * muSpikes: mu values for spike-in genes
 * mu0: Starting value for mu
 * delta0: Starting value for delta
 * phi0: Starting value for phi
 * s0: Starting value for s
 * nu0: Starting value for nu
 * theta0: Starting value for theta
 * s2mu: prior variance for mu
 * aphi: Dirichlet hyper-parameter for phi
 * as: prior shape for s 
 * bs: prior rate for s 
 * atheta: prior shape for theta
 * btheta: prior rate for theta 
 * ar: Optimal acceptance rate for adaptive Metropolis-Hastings updates
 * LSmu0: Starting value of adaptive proposal variance of mu (log-scale)
 * LSdelta0: Starting value of adaptive proposal variance of delta (log-scale)
 * LSphi0: Starting value of adaptive proposal precision of phi (log-scale)
 * LSnu0: Starting value of adaptive proposal variance of nu (log-scale)
 * LStheta0: Starting value of adaptive proposal variance of theta (log-scale)  
 * sumByCellAll: Sum of expression counts by cell (all genes)
 * sumByCellBio:  Sum of expression counts by cell (biological genes only)
 * sumByGeneAll: Sum of expression counts by gene (all genes)
 * sumByGeneBio: Sum of expression counts by gene (biological genes only)
 * StoreAdapt: whether to store adaptive variances 
 * EndAdapt: when to stop the adaptation
 * PrintProgress: whether to print progress report 
 * k: Number of regression components; k-2 Gaussian radial basis functions (GRBFs)
 * m0: Starting values for GRBFs coefficients
 * V0: Starting value for covariance matrix 
 * sigma2_a0: Prior shape for Gamma distribution
 * sigma2_b0: Prior rate for Gamma distribution
 * beta0: Starting values for regression parameters (weights)
 * sigma20: Starting value for regression variance
 * eta0: Fixed value for degrees of freedom
 * lambda0: Starting values for gene-wise error term
 * variance: Fixed width (scale) for GRBFs
 */
// [[Rcpp::export(".BASiCS_MCMCcppRegNoSpikes")]]
Rcpp::List BASiCS_MCMCcppRegNoSpikes(
    int N, 
    int Thin, 
    int Burn,  
    arma::mat Counts,  
    arma::mat BatchDesign, 
    arma::vec mu0, 
    arma::vec delta0, 
    arma::vec s0, 
    arma::vec nu0, 
    arma::vec theta0,
    arma::vec mu_mu,
    double s2mu,
    double as,
    double bs,
    double atheta, 
    double btheta, 
    int k,
    arma::vec m0, 
    arma::mat V0, 
    double sigma2_a0, 
    double sigma2_b0,
    arma::vec beta0, 
    double sigma20, 
    double eta0, 
    arma::vec lambda0, 
    double const& variance,
    double Constrain,
    arma::vec Index,
    int RefGene,
    arma::vec RefGenes,
    arma::vec ConstrainGene,
    arma::vec NotConstrainGene,
    int StochasticRef,
    double ar, 
    arma::vec LSmu0, 
    arma::vec LSdelta0, 
    arma::vec LSnu0, 
    arma::vec LStheta0, 
    arma::vec sumByCellAll, 
    arma::vec sumByGeneAll, 
    int StoreAdapt, 
    int EndAdapt,
    int PrintProgress,
    bool RBFMinMax,
    bool FixLocations,
    arma::vec RBFLocations,
    double const& mintol_mu,
    double const& mintol_delta,
    double const& mintol_nu,
    double const& mintol_theta,
    double const& geneExponent,
    double const& cellExponent,
    bool fixNu,
    int threads = 1) {


  #if defined(_OPENMP)
    omp_set_num_threads(threads);
  #endif

  using arma::ones;
  using arma::zeros;
  using Rcpp::Rcout; 
  
  // NUMBER OF CELLS, GENES AND STORED DRAWS
  int n = Counts.n_cols; 
  int nBatch = BatchDesign.n_cols;
  int q0 = delta0.n_elem;
  int Naux = N/Thin - Burn/Thin;
  
  // Transformation to uvec elements
  arma::uvec ConstrainGene_uvec = arma::conv_to<arma::uvec>::from(ConstrainGene);
  arma::uvec NotConstrainGene_uvec = arma::conv_to<arma::uvec>::from(NotConstrainGene);
  
  // OTHER GLOBAL QUANTITIES
  arma::vec BatchSizes = sum(BatchDesign,0).t();
  arma::mat inv_V0 = inv(V0);
  double mInvVm0 = Rcpp::as<double>(wrap(m0.t()*inv_V0*m0));
  arma::vec InvVm0 = inv_V0*m0;
  
  // OBJECTS WHERE DRAWS WILL BE STORED
  arma::mat mu = zeros(q0, Naux); 
  arma::mat delta = zeros(q0, Naux); 
  arma::mat s = ones(n, Naux);
  arma::mat nu = zeros(n, Naux); 
  arma::mat theta = zeros(nBatch, Naux); 
  arma::mat LSmu;
  arma::mat LSdelta;
  arma::mat LSnu;
  arma::mat LStheta; 
  
  // LOG-PROPOSAL VARIANCES 
  if(StoreAdapt == 1) {
    LSmu = zeros(q0, Naux); 
    LSdelta = zeros(q0, Naux); 
    LSnu = zeros(n, Naux); 
    LStheta = zeros(nBatch, Naux);   
  }
  
  // ACCEPTANCE RATES FOR ADAPTIVE METROPOLIS-HASTINGS UPDATES
  arma::vec muAccept = zeros(q0); 
  arma::vec deltaAccept = zeros(q0); 
  arma::vec nuAccept = zeros(n); 
  arma::vec thetaAccept = zeros(nBatch); 
  
  arma::vec PmuAux = zeros(q0);
  arma::vec PdeltaAux = zeros(q0);
  arma::vec PnuAux = zeros(n);
  arma::vec PthetaAux = zeros(nBatch);
  
  // INITIALIZATION OF PARAMETER VALUES FOR MCMC RUN
  arma::mat muAux = zeros(q0,2); muAux.col(0) = mu0; 
  arma::mat deltaAux = zeros(q0,2); deltaAux.col(0) = delta0; 
  arma::vec sAux = s0; 
  arma::mat nuAux = zeros(n,2);  nuAux.col(0) = nu0;
  arma::mat thetaAux = zeros(nBatch, 2); thetaAux.col(0) = theta0;
  arma::vec thetaBatch = BatchDesign * theta0; 
  
  // INITIALIZATION OF ADAPTIVE VARIANCES
  arma::vec LSmuAux = LSmu0;
  arma::vec LSdeltaAux = LSdelta0; 
  arma::vec LSnuAux = LSnu0;
  arma::vec LSthetaAux = LStheta0;  
  
  // OTHER AUXILIARY QUANTITIES FOR ADAPTIVE METROPOLIS UPDATES
  arma::vec PmuAux0 = zeros(q0); arma::vec PdeltaAux0 = zeros(q0);
  arma::vec PnuAux0 = zeros(n); arma::vec PthetaAux0 = zeros(nBatch);
  
  // BATCH INITIALIZATION FOR ADAPTIVE METROPOLIS UPDATES 
  // (RE-INITIALIZE EVERY 50 ITERATIONS)
  int Ibatch = 0; 
  
  // INITIALIZATION OF PARAMETERS TO RETURN IN UPDATE FUNCTIONS
  // To avoid repeated initialisation
  arma::vec y_q0 = ones(q0); arma::vec y_n = ones(n); 
  arma::vec ind_q0 = zeros(q0); arma::vec ind_n = zeros(n);
  arma::vec u_q0 = zeros(q0); arma::vec u_n = zeros(n);
  
  // INITIALIZATION OF PARAMETERS RELATED TO STOCHASTIC REF
  arma::vec RefFreq = arma::zeros(q0); 
  int RefAux;
  
  // REGRESSION SPECIFIC SECTION
  
  // Parameters for regression
  arma::mat beta = arma::zeros(k, Naux);
  arma::mat lambda = arma::zeros(q0, Naux);
  arma::vec sigma = arma::zeros(Naux);
  arma::mat epsilon = arma::zeros(q0, Naux);
  
  // INITIALIZATION OF REGRESSION PARAMETERS
  arma::vec mAux = m0; 
  arma::mat VAux = V0; 
  arma::vec betaAux = beta0;
  arma::vec lambdaAux = lambda0; 
  double sigma2Aux = sigma20; 
  arma::vec epsilonAux = arma::zeros(q0);
  
  double globalExponent = 1;
  if (geneExponent != 1) {
    globalExponent = geneExponent;
  } else if (cellExponent != 1) {
    globalExponent = cellExponent;
  }

  // OTHER PARAMETERS FOR REGRESSION
  arma::mat V1 = arma::zeros(k,k);
  // Model matrix initialization
  arma::vec means = muAux.col(0);
  if (!FixLocations) {
    RBFLocations = estimateRBFLocations(log(means), k, RBFMinMax);
  }
  arma::mat X = designMatrix(k, RBFLocations, means, variance);
  
  StartSampler(N);
  
  // START OF MCMC LOOP
  for (int i=0; i<N; i++) {
    
    Rcpp::checkUserInterrupt();
    
    if (i == Burn) {
      EndBurn();
    }
    
    Ibatch++; 
    
    if (!fixNu) {
      // UPDATE OF PHI
      // WE CAN RECYCLE THE SAME FULL CONDITIONAL AS IMPLEMENTED FOR S (BATCH CASE)
      sAux = sUpdateBatch(
        sAux,
        nuAux.col(0),
        thetaBatch,
        as,
        bs,
        BatchDesign,
        n,
        y_n,
        cellExponent
      );

      // UPDATE OF THETA: 
      // 1st ELEMENT IS THE UPDATE, 
      // 2nd ELEMENT IS THE ACCEPTANCE INDICATOR
      thetaAux = thetaUpdateBatch(
        thetaAux.col(0),
        exp(LSthetaAux),
        BatchDesign,
        BatchSizes,
        sAux,
        nuAux.col(0),
        atheta,
        btheta,
        n,
        nBatch,
        globalExponent,
        mintol_theta
      );

      PthetaAux += thetaAux.col(1);
      if(i>=Burn) {
        thetaAccept += thetaAux.col(1);
      }
      thetaBatch = BatchDesign * thetaAux.col(0); 
    }
    
    // UPDATE OF MU: 
    // 1st COLUMN IS THE UPDATE, 
    // 2nd COLUMN IS THE ACCEPTANCE INDICATOR 
    // If using stochastic reference, randomly select 1 ref gene
    if(StochasticRef == 1) {
      RefAux = as_scalar(arma::randi(1,arma::distr_param(0,RefGenes.size()-1) ));
      RefGene = RefGenes(RefAux); 
      if(i >= Burn) {
        RefFreq(RefGene) += 1;
      }
    }
    muAux = muUpdateRegNoSpikes(
      muAux.col(0),
      exp(LSmuAux),
      Counts, deltaAux.col(0),
      1 / deltaAux.col(0),
      nuAux.col(0),
      sumByCellAll,
      mu_mu,
      s2mu,
      q0,
      n,
      y_q0,
      u_q0,
      ind_q0,
      Constrain,
      RefGene,
      ConstrainGene_uvec,
      NotConstrainGene_uvec,
      k,
      lambdaAux,
      betaAux,
      X,
      sigma2Aux,
      variance,
      FixLocations,
      RBFMinMax,
      RBFLocations,
      geneExponent,
      mintol_mu
    );
    PmuAux += muAux.col(1); if(i>=Burn) muAccept += muAux.col(1);
    
    // UPDATE OF DELTA: 
    // 1st COLUMN IS THE UPDATE, 
    // 2nd COLUMN IS THE ACCEPTANCE INDICATOR
    // REGRESSION
    // THIS REQUIRES A NEW FULL CONDITIONAL
    deltaAux = deltaUpdateRegNoSpikes(
      deltaAux.col(0),
      exp(LSdeltaAux),
      Counts,
      muAux.col(0),
      nuAux.col(0),
      q0,
      n,
      y_q0,
      u_q0,
      ind_q0,
      lambdaAux,
      X,
      sigma2Aux,
      betaAux,
      geneExponent,
      mintol_delta
    );

    PdeltaAux += deltaAux.col(1);
    if(i>=Burn) deltaAccept += deltaAux.col(1);
    
    // for now retaining order of updates
    if (!fixNu) {
        // UPDATE OF NU: 
      // 1st COLUMN IS THE UPDATE, 
      // 2nd COLUMN IS THE ACCEPTANCE INDICATOR
      nuAux = nuUpdateBatchNoSpikes(
        nuAux.col(0),
        exp(LSnuAux),
        Counts,
        BatchDesign,
        muAux.col(0),
        1 / deltaAux.col(0),
        sAux,
        thetaBatch,
        sumByGeneAll,
        q0,
        n,
        y_n,
        u_n,
        ind_n,
        cellExponent,
        mintol_nu
      );
      PnuAux += nuAux.col(1);
      if(i>=Burn) nuAccept += nuAux.col(1);
    }

    // UPDATES OF REGRESSION RELATED PARAMETERS
    V1 = (inv_V0 * geneExponent) + X.t() * diagmat(lambdaAux) * X;
    VAux = inv(V1);
    if((det(V1)!=0) & all(arma::eig_sym(sigma2Aux * VAux) > 0)) {
      mAux = X.t() * (lambdaAux % log(deltaAux.col(0))) + (InvVm0 * geneExponent);
      mAux = VAux * mAux;
      
      // UPDATES OF BETA AND SIGMA2 (REGRESSION RELATED PARAMETERS)
      betaAux = betaUpdateReg(
        sigma2Aux,
        VAux,
        mAux
      );
      sigma2Aux = sigma2UpdateReg(
        deltaAux.col(0),
        betaAux,
        lambdaAux,
        V1, 
        mInvVm0 * globalExponent,
        mAux,
        sigma2_a0,
        sigma2_b0,
        q0,
        globalExponent
      );
    }
    // UPDATE OF LAMBDA (REGRESSION RELATED PARAMETER)
    lambdaAux = lambdaUpdateReg(
      deltaAux.col(0),
      X,
      betaAux,
      sigma2Aux, 
      eta0,
      q0,
      y_q0,
      globalExponent
    );

    // UPDATE OF EPSILON 
    // Direct calculation conditional on regression related parameters
    epsilonAux = log(deltaAux.col(0)) - X * betaAux;

    // STOP ADAPTING THE PROPOSAL VARIANCES AFTER EndAdapt ITERATIONS
    if(i < EndAdapt) {
      // UPDATE OF PROPOSAL VARIANCES (ONLY EVERY 50 ITERATIONS)
      if(Ibatch==50) {
        PmuAux = PmuAux/50;
        PmuAux = -1 + 2*arma::conv_to<arma::mat>::from(PmuAux>ar);
        LSmuAux = LSmuAux + PmuAux*0.1;
        PdeltaAux=PdeltaAux/50; 
        PdeltaAux = -1 + 2*arma::conv_to<arma::mat>::from(PdeltaAux>ar);
        LSdeltaAux = LSdeltaAux + PdeltaAux*0.1;
        PnuAux = PnuAux/50; 
        PnuAux = -1 + 2*arma::conv_to<arma::mat>::from(PnuAux>ar);
        LSnuAux = LSnuAux + PnuAux*0.1;
        PthetaAux = PthetaAux/50; 
        PthetaAux = -1 + 2*arma::conv_to<arma::mat>::from(PthetaAux>ar);
        LSthetaAux = LSthetaAux + PthetaAux*0.1;
        
        Ibatch = 0; 
        PmuAux = PmuAux0; PdeltaAux = PdeltaAux0; 
        PnuAux = PnuAux0; PthetaAux = PthetaAux0; 
        
        // REGRESSION
        // Update of model matrix every 50 iterations during Burn in period
        means = muAux.col(0);
        if (!FixLocations) {
          RBFLocations = estimateRBFLocations(log(means), k, RBFMinMax);
        }
        X = designMatrix(k, RBFLocations, means, variance);
      }
    }
    
    // STORAGE OF DRAWS
    if((i%Thin==0) & (i>=Burn)) {      
      mu.col(i/Thin - Burn/Thin) = muAux.col(0); 
      delta.col(i/Thin - Burn/Thin) = deltaAux.col(0); 
      s.col(i/Thin - Burn/Thin) = sAux;
      nu.col(i/Thin - Burn/Thin) = nuAux.col(0);       
      theta.col(i/Thin - Burn/Thin) = thetaAux.col(0);  
      
      // Regression
      beta.col(i/Thin - Burn/Thin) = betaAux;
      lambda.col(i/Thin - Burn/Thin) = lambdaAux;
      sigma(i/Thin - Burn/Thin) = sigma2Aux;
      epsilon.col(i/Thin - Burn/Thin) = epsilonAux;
      
      if(StoreAdapt == 1) {
        LSmu.col(i/Thin - Burn/Thin) = LSmuAux;
        LSdelta.col(i/Thin - Burn/Thin) = LSdeltaAux;
        LSnu.col(i/Thin - Burn/Thin) = LSnuAux;
        LStheta.col(i/Thin - Burn/Thin) = LSthetaAux; 
      }
    }
    
    // PRINT IN CONSOLE SAMPLED VALUES FOR FEW SELECTED PARAMETERS
    if((i%(2*Thin) == 0) & (PrintProgress == 1)) {
      CurrentIter(i, N);
      Rcout << "mu (gene 1): " << muAux(0,0) << std::endl; 
      Rcout << "delta (gene 1): " << deltaAux(0,0) << std::endl; 
      Rcout << "s (cell 1): " << sAux(0) << std::endl;
      Rcout << "nu (cell 1): " << nuAux(0,0) << std::endl;
      Rcout << "theta (batch 1): " << thetaAux(0,0) << std::endl;
      Rcpp::Rcout << "betas: " << betaAux.t() << std::endl;
      Rcpp::Rcout << "sigma: " << sigma2Aux << std::endl; 
      Rcpp::Rcout << "lambda (gene 1): " << lambdaAux(0) << std::endl;
      Rcpp::Rcout << "epsilon (gene 1): " << epsilonAux(0) << std::endl;
      Rcout << "-----------------------------------------------------" << std::endl;
      Rcout << "Current proposal variances for Metropolis Hastings updates (log-scale)." << std::endl;
      Rcout << "LSmu (gene 1): " << LSmuAux(0) << std::endl;
      Rcout << "LSdelta (gene 1): " << LSdeltaAux(0) << std::endl; 
      Rcout << "LSnu (cell 1): " << LSnuAux(0) << std::endl;
      Rcout << "LStheta (batch 1): " << LSthetaAux(0) << std::endl;
    }    
  }
  
  // END OF MCMC SAMPLER AND ACCEPTANCE RATE CONSOLE OUTPUT
  EndSampler(N); 
  ReportAR(muAccept/(N-Burn), "mu[i]'s");
  ReportAR(deltaAccept/(N-Burn), "delta[i]'s");  
  ReportAR(nuAccept/(N-Burn), "nu[jk]'s");  
  ReportAR(thetaAccept/(N-Burn), "theta[k]'s");  
  
  Rcout << " " << std::endl;
  Rcout << "-----------------------------------------------------" << std::endl;
  Rcout << " " << std::endl;
  
  if(StoreAdapt == 1) {
    // OUTPUT (AS A LIST)
    return(
      Rcpp::List::create(
        Rcpp::Named("mu") = mu.t(),
        Rcpp::Named("delta") = delta.t(),
        Rcpp::Named("s") = s.t(),
        Rcpp::Named("nu") = nu.t(),
        Rcpp::Named("theta") = theta.t(),
        Rcpp::Named("beta") = beta.t(),
        Rcpp::Named("sigma2") = sigma,
        Rcpp::Named("lambda") = lambda.t(),
        Rcpp::Named("epsilon") = epsilon.t(),
        Rcpp::Named("designMatrix") = X,
        Rcpp::Named("RBFLocations") = RBFLocations,
        Rcpp::Named("ls.mu") = LSmu.t(),
        Rcpp::Named("ls.delta") = LSdelta.t(),
        Rcpp::Named("ls.nu") = LSnu.t(),
        Rcpp::Named("ls.theta") = LStheta.t(),
        Rcpp::Named("RefFreq") = RefFreq/(N-Burn)
      )
    ); 
  }
  else {
    // OUTPUT (AS A LIST)
    return(
      Rcpp::List::create(
        Rcpp::Named("mu") = mu.t(),
        Rcpp::Named("delta") = delta.t(),
        Rcpp::Named("s") = s.t(),
        Rcpp::Named("nu") = nu.t(),
        Rcpp::Named("theta") = theta.t(),
        Rcpp::Named("beta") = beta.t(),
        Rcpp::Named("sigma2") = sigma,
        Rcpp::Named("lambda") = lambda.t(),
        Rcpp::Named("epsilon") = epsilon.t(),
        Rcpp::Named("RefFreq") = RefFreq / (N - Burn),
        Rcpp::Named("RBFLocations") = RBFLocations
      )
    );
  }
}

#endif
