/* BatchLearning.cpp
 * 
 * Kubo Ryosuke
 */

#ifndef NLEARN

#include "BatchLearning.h"
#include "./LearningConfig.h"
#include "config/Config.h"
#include "core/move/MoveGenerator.h"
#include "core/record/CsaReader.h"
#include "core/util/FileList.h"
#include <list>
#include <cstdlib>

#define TRAINING_DAT  "training.dat"

#define SEARCH_WINDOW 256
#define NORM          1.0e-2f

namespace sunfish {

namespace {

void setSearcherDepth(Searcher& searcher, int depth) {
  auto searchConfig = searcher.getConfig();
  searchConfig.maxDepth = depth;
  searcher.setConfig(searchConfig);
}

inline float gain() {
  return -7.0f / SEARCH_WINDOW;
}

inline float sigmoid(float x) {
  return 1.0 / (1.0 + std::exp(x * gain()));
}

inline float dsigmoid(float x) {
  float s = sigmoid(x);
  return (s - s * s) * gain();
}

inline float loss(float x) {
  return sigmoid(x);
}

inline float gradient(float x) {
  return dsigmoid(x);
}

inline float norm(float x) {
  if (x > 0.0f) {
    return -NORM;
  } else if (x < 0.0f) {
    return NORM;
  } else {
    return 0.0f;
  }
}

} // namespace

bool BatchLearning::openTrainingData() {
  trainingData_.reset(new std::ofstream);
  trainingData_->open(TRAINING_DAT, std::ios::binary | std::ios::out);

  if (!trainingData_) {
    Loggers::error << "open error!! [" << TRAINING_DAT << "]";
    return false;
  }

  return true;
}

void BatchLearning::closeTrainingData() {
  trainingData_->close();
}

/**
 * $B%W%m%0%l%9%P!<$NI=<($r99?7$7$^$9!#(B
 */
void BatchLearning::updateProgress() {
  int cmax = 50;

  std::cout << "\r";
  for (int c = 0; c < cmax; c++) {
    if (c * totalJobs_ <= cmax * completedJobs_) {
      std::cout << '#';
    } else {
      std::cout << ' ';
    }
  }
  float percentage = (float)completedJobs_ / totalJobs_ * 100.0f;
  std::cout << " [" << percentage << "%]";
  std::cout << std::flush;
}

/**
 * $B%W%m%0%l%9%P!<$NI=<($r=*N;$7$^$9!#(B
 */
void BatchLearning::closeProgress() {
  std::cout << "\n";
  std::cout << std::flush;
}

/**
 * $B71N}%G!<%?$r@8@.$7$^$9!#(B
 */
void BatchLearning::generateTraningData(int wn, Board board, Move move0) {
  // $B9gK!<j@8@.(B
  Moves moves;
  MoveGenerator::generate(board, moves);

  if (moves.size() < 2) {
    return;
  }

  Value val0;
  Move tmpMove;
  std::list<PV> list;

  // $B%R%9%H%j$N%/%j%"(B
  searchers_[wn]->clearHistory();

  {
    // $BC5:w(B
    board.makeMove(move0);
    setSearcherDepth(*searchers_[wn], config_.getInt(LCONF_DEPTH));
    searchers_[wn]->idsearch(board, tmpMove);
    board.unmakeMove(move0);

    // PV $B$HI>2ACM(B
    const auto& info = searchers_[wn]->getInfo();
    const auto& pv = info.pv;
    val0 = -info.eval;

    // $B5M$_$O=|30(B
    if (val0 <= -Value::Mate || val0 >= Value::Mate) {
      return;
    }

    list.push_back({});
    list.back().set(move0, 0, pv);
  }

  totalMoves_++;

  // $B4}Ih$N<j$NI>2ACM$+$i(B window $B$r7hDj(B
  Value alpha = val0 - SEARCH_WINDOW;
  Value beta = val0 + SEARCH_WINDOW;

  for (auto& move : moves) {
    // $BC5:w(B
    bool valid = board.makeMove(move);
    if (!valid) { continue; }
    setSearcherDepth(*searchers_[wn], config_.getInt(LCONF_DEPTH));
    searchers_[wn]->idsearch(board, tmpMove, -beta, -alpha);
    board.unmakeMove(move);

    // PV $B$HI>2ACM(B
    const auto& info = searchers_[wn]->getInfo();
    const auto& pv = info.pv;
    Value val = -info.eval;

    if (val <= alpha) {
      continue;
    }

    if (val >= beta) {
      outOfWindLoss_++;
      continue;
    }

    list.push_back({});
    list.back().set(move, 0, pv);
  }

  // $B=q$-=P$7(B
  if (!list.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);

    // $B%k!<%H6ILL(B
    CompactBoard cb = board.getCompactBoard();
    trainingData_->write(reinterpret_cast<char*>(&cb), sizeof(cb));

    for (const auto& pv : list) {
      // $B<j=g$ND9$5(B
      uint8_t length = static_cast<uint8_t>(pv.size()) + 1;
      trainingData_->write(reinterpret_cast<char*>(&length), sizeof(length));

      // $B<j=g(B
      for (size_t i = 0; i < pv.size(); i++) {
        uint16_t m = Move::serialize16(pv.get(i).move);
        trainingData_->write(reinterpret_cast<char*>(&m), sizeof(m));
      }
    }

    // $B=*C<(B
    uint8_t n = 0;
    trainingData_->write(reinterpret_cast<char*>(&n), sizeof(n));
  }
}

/**
 * $B71N}%G!<%?$r@8@.$7$^$9!#(B
 */
void BatchLearning::generateTraningData(int wn, const Job& job) {
  Record record;
  if (!CsaReader::read(job.path, record)) {
    Loggers::error << "Could not read csa file. [" << job.path << "]";
    exit(1);
  }

  // $B4}Ih$N@hF,$X(B
  while (record.unmakeMove())
    ;

  while (true) {
    // $B<!$N(B1$B<j$r<hF@(B
    Move move = record.getNextMove();
    if (move.isEmpty()) {
      break;
    }

    generateTraningData(wn, record.getBoard(), move);

    // 1$B<j?J$a$k(B
    if (!record.makeMove()) {
      break;
    }
  }
}

/**
 * $B%8%g%V$r=&$$$^$9!#(B
 */
void BatchLearning::work(int wn) {
  while (!shutdown_) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));

    Job job;

    // dequeue
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (jobQueue_.empty()) {
        continue;
      }
      job = jobQueue_.front();
      jobQueue_.pop();
      activeCount_++;
    }

    generateTraningData(wn, job);
 
    completedJobs_++;
    activeCount_--;

    {
      std::lock_guard<std::mutex> lock(mutex_);
      updateProgress();
    }
  }
}

/**
 * $B%8%g%V$r:n@.$7$^$9!#(B
 */
bool BatchLearning::generateJobs() {
  FileList fileList;
  std::string dir = config_.getString(LCONF_KIFU);
  fileList.enumerate(dir.c_str(), "csa");

  if (fileList.size() == 0) {
    Loggers::error << "no files.";
    return false;
  }

  completedJobs_ = 0;
  totalJobs_ = fileList.size();

  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& path : fileList) {
      jobQueue_.push({ path });
    }
  }

  return true;
}

/**
 * $B%o!<%+!<$,%8%g%V$r=*$($k$^$GBT5!$7$^$9!#(B
 */
void BatchLearning::waitForWorkers() {
  while (true) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (jobQueue_.empty() && activeCount_ == 0) {
        return;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

/**
 * $B8{G[%Y%/%H%k$r@8@.$7$^$9!#(B
 */
bool BatchLearning::generateGradient() {
  std::ifstream trainingData;

  trainingData.open(TRAINING_DAT);
  if (!trainingData) {
    Loggers::error << "open error!! [" << TRAINING_DAT << "]";
    return false;
  }

  g_.init();

  while (true) {
    // $B%k!<%H6ILL(B
    CompactBoard cb;
    trainingData.read(reinterpret_cast<char*>(&cb), sizeof(cb));

    if (trainingData.eof()) {
      break;
    }

    const Board root(cb);
    const bool black = root.isBlack();

    auto readPV = [&trainingData](Board& board) {
      // $B<j=g$ND9$5(B
      uint8_t length;
      trainingData.read(reinterpret_cast<char*>(&length), sizeof(length));
      if (length == 0) {
        return false;
      }
      length--;

      // $B<j=g(B
      bool ok = true;
      for (uint8_t i = 0; i < length; i++) {
        uint16_t m;
        trainingData.read(reinterpret_cast<char*>(&m), sizeof(m));
        Move move = Move::deserialize16(m, board);
        if (!ok || move.isEmpty() || !board.makeMove(move)) {
          ok = false;
        }
      }

      return true;
    };

    Board board0 = root;
    readPV(board0);
    Value val0 = eval_.evaluate(board0).value();

    while (true) {
      Board board = root;
      if (!readPV(board)) {
        break;
      }
      Value val = eval_.evaluate(board).value();

      float diff = val.int32() - val0.int32();
      diff = black ? diff : -diff;

      loss_ += loss(diff);

      float g = gradient(diff);
      g = black ? g : -g;
      g_.extract<float, true>(board0, g);
      g_.extract<float, true>(board, -g);
    }
  }

  trainingData.close();
  return true;
}

/**
 * $B%Q%i%a!<%?$r99?7$7$^$9!#(B
 */
void BatchLearning::updateParameters() {
  auto update = [this](FV::ValueType& g, Evaluator::ValueType& e,
      Evaluator::ValueType& max, uint64_t& magnitude) {
    g += norm(e);
    if (g > 0.0f) {
      e += rand_.getBit() + rand_.getBit();
    } else if (g < 0.0f) {
      e -= rand_.getBit() + rand_.getBit();
    }
    Evaluator::ValueType abs = std::abs(e);
    max = std::max(max, abs);
    magnitude = magnitude + abs;
  };

  max_ = 0;
  magnitude_ = 0;

  for (int i = 0; i < KPP_ALL; i++) {
    update(((FV::ValueType*)g_.t_->kpp)[i],
           ((Evaluator::ValueType*)eval_.t_->kpp)[i],
           max_, magnitude_);
  }

  for (int i = 0; i < KKP_ALL; i++) {
    update(((FV::ValueType*)g_.t_->kkp)[i],
           ((Evaluator::ValueType*)eval_.t_->kkp)[i],
           max_, magnitude_);
  }

  // $B%O%C%7%eI=$r=i4|2=(B
  eval_.clearCache();
  for (uint32_t wn = 0; wn < nt_; wn++) {
    searchers_[wn]->clearTT();
  }
}

/**
 * $B%P%C%A3X=,$NH?I|=hM}$r<B9T$7$^$9!#(B
 */
bool BatchLearning::iterate() {
  const int iterateCount = config_.getInt(LCONF_ITERATION);
  int  updateCount = 256;

  for (int i = 0; i < iterateCount; i++) {
    if (!openTrainingData()) {
      return false;
    }

    totalMoves_ = 0;
    outOfWindLoss_ = 0;

    if (!generateJobs()) {
      return false;
    }

    waitForWorkers();

    closeTrainingData();
    closeProgress();

    updateCount = std::max(updateCount / 2, 16);

    for (int j = 0; j < updateCount; j++) {
      loss_ = 0.0f;

      if (!generateGradient()) {
        return false;
      }

      updateParameters();

      float elapsed = timer_.get();
      float outOfWindLoss = (float)outOfWindLoss_ / totalMoves_;
      float totalLoss = ((float)outOfWindLoss_ + loss_) / totalMoves_;

      Loggers::message
        << "elapsed=" << elapsed
        << "\titeration=" << i << "," << j
        << "\tout_wind_loss=" << outOfWindLoss
        << "\tloss=" << totalLoss
        << "\tmax=" << max_
        << "\tmagnitude=" << magnitude_;
    }

    // $BJ]B8(B
    eval_.writeFile();

    // $B%-%c%C%7%e%/%j%"(B
    eval_.clearCache();
  }

  return true;
}

/**
 * $B3X=,$r<B9T$7$^$9!#(B
 */
bool BatchLearning::run() {
  Loggers::message << "begin learning";

  timer_.set();

  // $B=i4|2=(B
  eval_.init();

  // $B3X=,%9%l%C%I?t(B
  nt_ = config_.getInt(LCONF_THREADS);

  // Searcher$B@8@.(B
  searchers_.clear();
  for (uint32_t wn = 0; wn < nt_; wn++) {
    searchers_.emplace_back(new Searcher(eval_));

    auto searchConfig = searchers_.back()->getConfig();
    searchConfig.workerSize = 1;
    searchConfig.treeSize = Searcher::standardTreeSize(searchConfig.workerSize);
    searchConfig.enableLimit = false;
    searchConfig.enableTimeManagement = false;
    searchConfig.ponder = false;
    searchConfig.logging = false;
    searchers_.back()->setConfig(searchConfig);
  }

  activeCount_ = 0;

  // $B%o!<%+!<%9%l%C%I@8@.(B
  shutdown_ = false;
  threads_.clear();
  for (uint32_t wn = 0; wn < nt_; wn++) {
    threads_.emplace_back(std::bind(std::mem_fn(&BatchLearning::work), this, wn));
  }

  bool ok = iterate();

  // $B%o!<%+!<%9%l%C%IDd;_(B
  shutdown_ = true;
  for (uint32_t wn = 0; wn < nt_; wn++) {
    threads_[wn].join();
  }

  if (!ok) {
    return false;
  }

  Loggers::message << "completed..";

  float elapsed = timer_.get();
  Loggers::message << "elapsed: " << elapsed;
  Loggers::message << "end learning";

  return true;
}

}

#endif // NLEARN
