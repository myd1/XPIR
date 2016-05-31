#include "libpir.hpp"



  imported_database::~imported_database(){
    for (unsigned int i = 0 ; i < nbElements ; i++){
      free(((lwe_in_data *)imported_database_ptr)[i].p[0]);
      free(((lwe_in_data *)imported_database_ptr)[i].p);
    }
    free(imported_database_ptr);
  }



/** 
*	HomomorphicCryptoFactory is used to create a generic cryptographic object (LWE, Paillier, 
*	mocked-cryptography, etc.). This API only exposes the Ring-LWE cryptosystem, but we still 
*	use the generic factory to get an instance of this cryptosystem to avoid code duplication.
**/
  HomomorphicCrypto* HomomorphicCryptoFactory::getCryptoMethod(std::string cryptoType)
  {
    return HomomorphicCryptoFactory_internal::getCryptoMethod(cryptoType);
  }
  
  void HomomorphicCryptoFactory::printAllCryptoParams() {
	  std::cout << "Available crypto parameters : (CryptoSystem::SecurityMax::PolyDegree::ModulusBitsize)" << std::endl;  
	  NFLLWE nfl; 
	  std::set<std::string> crypto_params_set;
	  unsigned int i=nfl.getAllCryptoParams(crypto_params_set);
	  for(auto const& crypto: crypto_params_set) {
		  std::cout << crypto << std::endl;
	  }
	  std::cout<<std::endl;
  }



/** 
*	PIRQueryGenerator is Client side, it initiates the PIR protocol by generating a query
*	corresponding to the chosen element
**/
	PIRQueryGenerator::PIRQueryGenerator(PIRParameters& pirParameters, HomomorphicCrypto& cryptoMethod_) :
    PIRQueryGenerator_internal(pirParameters,cryptoMethod_, false){

	}

  
	void PIRQueryGenerator::generateQuery(uint64_t _chosenElement ) {
		nbQueries=0;
		for(int i=0;i<pirParams.d;i++) nbQueries+=pirParams.n[i];
		PIRQueryGenerator_internal::setChosenElement(_chosenElement );
		PIRQueryGenerator_internal::generateQuery();
	}	
	

	bool PIRQueryGenerator::popQuery(char** query) {
		if(nbQueries!=0) {
			*query=queryBuffer.pop_front();
			nbQueries--;
			return true;
		} else {
			return false;
		}
	}

  uint64_t PIRQueryGenerator::getQueryElementBytesize()
  {
    return cryptoMethod.getPublicParameters().getCiphertextBitsize()/8; 
  }




/** 
*	PIRReplyGenerator is Server side, it handles the request generated by the client and generates the reply
**/
  PIRReplyGenerator::PIRReplyGenerator(PIRParameters& param, HomomorphicCrypto& cryptoMethod_, DBHandler *db) 
	: PIRReplyGeneratorNFL_internal (param,db, false)
  {
		PIRReplyGeneratorNFL_internal::setCryptoMethod(&cryptoMethod_);
    PIRReplyGeneratorNFL_internal::setPirParams(param);
  }

  
  void PIRReplyGenerator::pushQuery(char* rawQuery) {
		PIRReplyGeneratorNFL_internal::pushQuery(rawQuery);
  }
	
	
  imported_database* PIRReplyGenerator::importData(uint64_t offset, uint64_t bytes_per_db_element) {
		uint64_t   usable_memory = getTotalSystemMemory();
		if(bytes_per_db_element*dbhandler->getNbStream()*4>usable_memory/10) {
			std::cerr<<"WARNING: going to use more than one tenth of the available memory for storing the precomputed data, maybe you should reduce bytes_per_db_element when calling importData"<<std::flush<<std::endl;
		}
		importDataNFL(offset,bytes_per_db_element);
		imported_database* precomputed = new imported_database();
		precomputed->imported_database_ptr=input_data;

		precomputed->nbElements = ceil((float)dbhandler->getNbStream()/pirParam.alpha);	
  	  	precomputed->polysPerElement = currentMaxNbPolys; 
		precomputed->beforeImportElementBytesize=bytes_per_db_element;
		return precomputed;
  }
	

  void PIRReplyGenerator::generateReply(const imported_database* database)
  {
    // Init
		nbRepliesToHandle=0;
		nbRepliesGenerated=0;
		currentReply=0;
    freeResult();

    // Test memory
		uint64_t usable_memory = getTotalSystemMemory();
		nbRepliesGenerated=nbRepliesToHandle=computeReplySizeInChunks(database->beforeImportElementBytesize);
		uint64_t polysize = cryptoMethod->getpolyDegree() * cryptoMethod->getnbModuli()*sizeof(uint64_t);
		uint64_t sizeOfReply=nbRepliesToHandle*polysize;
		if(sizeOfReply>usable_memory/10) {
			std::cerr<<"WARNING: going to use more than one tenth of the available memory for storing the reply"<<std::flush<<std::endl;
		}

		input_data = (lwe_in_data*) database->imported_database_ptr;
		currentMaxNbPolys = database->polysPerElement;
    
   	// The internal generator is locked by default waiting for the query to be received 
    // in this API we let the user deal with synchronisation so the lock is not needed
    PIRReplyGeneratorNFL_internal::mutex.try_lock();
    PIRReplyGeneratorNFL_internal::mutex.unlock();
    
    // Define the reply size
    repliesAmount = computeReplySizeInChunks(database->beforeImportElementBytesize);
		PIRReplyGeneratorNFL_internal::generateReply();

  }

  void PIRReplyGenerator::freeQueries(){
    PIRReplyGeneratorNFL_internal::freeQueries();
  }

	
  /** popReply that return false when the queue is over (true otherwise) and waits when its empty
  **/
  bool PIRReplyGenerator::popReply(char** reply) {
		// For each ciphertext in the reply
		if(nbRepliesToHandle--<=0) return false;
		while (repliesArray == NULL || repliesArray[currentReply] == NULL)   { 
			boost::this_thread::sleep(boost::posix_time::milliseconds(10));
		}
		*reply = repliesArray[currentReply];
		repliesArray[currentReply++]=NULL;
		return true;
		
  }

  uint64_t PIRReplyGenerator::getnbRepliesGenerated() {
    return nbRepliesGenerated;
  }

  uint64_t PIRReplyGenerator::getReplyElementBytesize()
  {
    return cryptoMethod->getPublicParameters().getCiphertextBitsize()/8; 
  }




/** 
*	PIRReplyExtraction is Client side, it extracts the chosen element from the reply of the Server
**/
  PIRReplyExtraction::PIRReplyExtraction(PIRParameters& pirParameters, HomomorphicCrypto& cryptoMethod_):
    PIRReplyExtraction_internal(pirParameters,cryptoMethod_, false), clearChunks("clearChunks"){
    nbPlaintextReplies=0;
  }
	
  void PIRReplyExtraction::pushEncryptedReply(char* rawBytes) {
		repliesBuffer.push(rawBytes);
  }
	
  void PIRReplyExtraction::extractReply(uint64_t maxFileBytesize){
		PIRReplyExtraction_internal::extractReply(pirParams.alpha*maxFileBytesize,&clearChunks);
    nbPlaintextReplies = getnbPlaintextReplies(maxFileBytesize);
  }
			
  bool PIRReplyExtraction::popPlaintextResult(char** result) {
		if(nbPlaintextReplies!=0) {
			*result=clearChunks.pop_front();
			nbPlaintextReplies--;
			return true;
		} else {
			return false;
		}
  }

  uint64_t PIRReplyExtraction::getPlaintextReplyBytesize(){
    return cryptoMethod.getPublicParameters().getAbsorptionBitsize(0)/GlobalConstant::kBitsPerByte;
  }
  
  uint64_t PIRReplyExtraction::getnbPlaintextReplies(uint64_t maxFileBytesize) {
    return ceil((float)maxFileBytesize*pirParams.alpha/getPlaintextReplyBytesize());
  }
  


