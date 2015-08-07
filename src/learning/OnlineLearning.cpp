/* OnlineLearning.cpp
 * 
 * Kubo Ryosuke
 */

#ifndef NLEARN

#include "./OnlineLearning.h"
#include "./LearningConfig.h"
#include "config/Config.h"
#include "core/move/MoveGenerator.h"
#include "core/record/CsaReader.h"
#include "core/util/FileList.h"
#include "core/def.h"
#include "logger/Logger.h"
#include "searcher/progress/Progression.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>

#define MAX_HINGE_MARGIN        256
#define MIN_HINGE_MARGIN        10
#define NUMBER_OF_SIBLING_NODES 16
#define MINI_BATCH_LENGTH       256
#define NORM                    1.0e-6f
#define GRADIENT                4.0f

namespace sunfish {

namespace {

Board getPVLeaf(const Board& root, const Move& rmove, const PV& pv) {
  Board board = root;
  board.makeMoveIrr(rmove);
  for (int d = 0; d < pv.size(); d++) {
    Move move = pv.get(d).move;
    if (move.isEmpty() || !board.makeMove(move)) {
      break;
    }
  }
  return board;
}

inline int hingeMargin(const Board& board) {
  float prog = (float)Progression::evaluate(board) / Progression::Scale;
  float margin = MIN_HINGE_MARGIN + (MAX_HINGE_MARGIN - MIN_HINGE_MARGIN) * prog;
  assert(margin >= MIN_HINGE_MARGIN);
  assert(margin <= MAX_HINGE_MARGIN);
  return std::round(margin);
}

inline float gradient() {
  return GRADIENT * ValuePair::PositionalScale;
}

inline float error(float x) {
  return x * gradient();
}

inline float norm(float x) {
  CONSTEXPR_CONST float n = NORM * ValuePair::PositionalScale;
  if (x > 0.0f) {
    return -n;
  } else if (x < 0.0f) {
    return n;
  } else {
    return 0.0f;
  }
}

} // namespace

/**
 * $B8{G[$r7W;;$7$^$9!#(B
 */
void OnlineLearning::genGradient(int wn, const Job& job) {
  Board board(job.board);
  Move move0 = job.move;
  Value val0;
  PV pv0;
  Move tmpMove;

  bool black = board.isBlack();

  // $B9gK!<j@8@.(B
  Moves moves;
  MoveGenerator::generate(board, moves);

  if (moves.size() < 2) {
    return;
  }

  // $B%7%c%C%U%k(B
  std::shuffle(moves.begin(), moves.end(), rgens_[wn]);

  searchers_[wn]->clearHistory();

  // $B4}Ih$N<j(B
  {
    // $BC5:w(B
    board.makeMove(move0);
    searchers_[wn]->search(board, tmpMove);
    board.unmakeMove(move0);

    // PV $B$HI>2ACM(B
    const auto& info = searchers_[wn]->getInfo();
    const auto& pv = info.pv;
    val0 = -info.eval;
    pv0.copy(pv);

    // $B5M$_$O=|30(B
    if (val0 <= -Value::Mate || val0 >= Value::Mate) {
      return;
    }
  }

  // $B4}Ih$N<j$NI>2ACM$+$i(B window $B$r7hDj(B
  Value alpha = val0 - hingeMargin(board);
  Value beta = val0 + MAX_HINGE_MARGIN;

  // $B$=$NB>$N<j(B
  int count = 0;
  float gsum = 0.0f;
  for (auto& move : moves) {
    if (count >= NUMBER_OF_SIBLING_NODES) {
      break;
    }

    // $BC5:w(B
    bool valid = board.makeMove(move);
    if (!valid) { continue; }
    searchers_[wn]->search(board, tmpMove, -beta, -alpha);
    board.unmakeMove(move);

    count++;

    // PV $B$HI>2ACM(B
    const auto& info = searchers_[wn]->getInfo();
    const auto& pv = info.pv;
    Value val = -info.eval;

    // $BIT0lCWEY$N7WB,(B
    errorCount_++;
    errorSum_ += error(std::min(std::max(val.int32(), alpha.int32()), beta.int32()) - alpha.int32());

    // window $B$r30$l$?>l9g$O=|30(B
    if (val <= alpha || val >= beta) {
      continue;
    }

    // leaf $B6ILL(B
    Board leaf = getPVLeaf(board, move, pv);

    // $BFCD'Cj=P(B
    float g = gradient() * (black ? 1 : -1);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      g_.extract<float, true>(leaf, -g);
    }
    gsum += g;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);

    // leaf $B6ILL(B
    Board leaf = getPVLeaf(board, move0, pv0);

    // $BFCD'Cj=P(B
    g_.extract<float, true>(leaf, gsum);

    miniBatchScale_ += NUMBER_OF_SIBLING_NODES;
  }
}

/**
 * $B%8%g%V$r=&$$$^$9!#(B
 */
void OnlineLearning::work(int wn) {
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

    genGradient(wn, job);

    activeCount_--;
  }
}

/**
 * $B%_%K%P%C%A$r<B9T$7$^$9!#(B
 */
bool OnlineLearning::miniBatch() {

  if (jobs_.size() < MINI_BATCH_LENGTH) {
    return false;
  }

  Loggers::message << "jobs=" << jobs_.size();

  miniBatchScale_ = 0;
  errorCount_ = 0;
  errorSum_ = 0.0f;

  {
    std::lock_guard<std::mutex> lock(mutex_);

    for (int i = 0; i < MINI_BATCH_LENGTH; i++) {
      jobQueue_.push(jobs_.back());
      jobs_.pop_back();
    }
  }

  // $B%-%e!<$,6u$K$J$k$N$rBT$D(B
  while (true) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (jobQueue_.empty() && activeCount_ == 0) {
        break;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  Evaluator::ValueType max = 0;
  int64_t magnitude = 0ll;
  int32_t nonZero = 0;
  FV::ValueType maxW = 0.0f;
  double magnitudeW = 0.0f;
  FV::ValueType maxU = 0.0f;

  // $B8{G[$K=>$C$FCM$r99?7$9$k(B
  auto update1 = [this](FV::ValueType& g, FV::ValueType& w, FV::ValueType& u,
      FV::ValueType& maxW, double& magnitudeW, FV::ValueType& maxU) {
    FV::ValueType f = g / miniBatchScale_ + norm(w);
    g = 0.0f;
    w += f;
    u += f * miniBatchCount_;
    maxW = std::max(maxW, std::abs(w));
    magnitudeW += std::abs(w);
    maxU = std::max(maxU, std::abs(u));
  };
  for (int i = 0; i < KPP_ALL; i++) {
    update1(((FV::ValueType*)g_.t_->kpp)[i],
            ((FV::ValueType*)w_.t_->kpp)[i],
            ((FV::ValueType*)u_.t_->kpp)[i],
            maxW, magnitudeW, maxU);
  }
  for (int i = 0; i < KKP_ALL; i++) {
    update1(((FV::ValueType*)g_.t_->kkp)[i],
            ((FV::ValueType*)w_.t_->kkp)[i],
            ((FV::ValueType*)u_.t_->kkp)[i],
            maxW, magnitudeW, maxU);
  }

  miniBatchCount_++;

  // $BJ?6Q2=(B
  auto average = [this](const FV::ValueType& w, const FV::ValueType& u, Evaluator::ValueType& e,
      Evaluator::ValueType& max, int64_t& magnitude, int32_t& nonZero) {
    e = std::round(w - u / miniBatchCount_);
    max = std::max(max, (Evaluator::ValueType)std::abs(e));
    magnitude += std::abs(e);
    nonZero += e != 0 ? 1 : 0;
  };
  for (int i = 0; i < KPP_ALL; i++) {
    average(((FV::ValueType*)w_.t_->kpp)[i],
            ((FV::ValueType*)u_.t_->kpp)[i],
            ((Evaluator::ValueType*)eval_.t_->kpp)[i],
            max, magnitude, nonZero);
  }
  for (int i = 0; i < KKP_ALL; i++) {
    average(((FV::ValueType*)w_.t_->kkp)[i],
            ((FV::ValueType*)u_.t_->kkp)[i],
            ((Evaluator::ValueType*)eval_.t_->kkp)[i],
            max, magnitude, nonZero);
  }

  // $BJ]B8(B
  eval_.writeFile();

  // $B:G8e$N(Bw$B$NCM$G99?7$9$k(B
  auto update2 = [this](FV::ValueType& w, Evaluator::ValueType& e) {
    e = std::round(w);
  };
  for (int i = 0; i < KPP_ALL; i++) {
    update2(((FV::ValueType*)w_.t_->kpp)[i],
            ((Evaluator::ValueType*)eval_.t_->kpp)[i]);
  }
  for (int i = 0; i < KKP_ALL; i++) {
    update2(((FV::ValueType*)w_.t_->kkp)[i],
            ((Evaluator::ValueType*)eval_.t_->kkp)[i]);
  }

  float error = errorSum_ / errorCount_;
  float elapsed = timer_.get();
  Loggers::message
    << "mini_batch_count=" << (miniBatchCount_ - 1)
    << "\terror=" << error
    << "\tmax=" << max
    << "\tmagnitude=" << magnitude
    << "\tnon_zero=" << nonZero
    << "\tmax_w=" << maxW
    << "\tmagnitude_w=" << magnitudeW
    << "\tmax_u=" << maxU
    << "\telapsed: " << elapsed;

  // $B%O%C%7%eI=$r=i4|2=(B
  eval_.clearCache();
  for (uint32_t wn = 0; wn < nt_; wn++) {
    searchers_[wn]->clearTT();
  }

  return true;
}

/**
 * $B4}Ih%U%!%$%k$rFI$_9~$s$G3X=,$7$^$9!#(B
 */
bool OnlineLearning::readCsa(size_t count, size_t total, const char* path) {
  Loggers::message << "loading (" << count << "/" << total << "): [" << path << "]";

  Record record;
  if (!CsaReader::read(path, record)) {
    Loggers::warning << "Could not read csa file. [" << path << "]";
    return false;
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

    jobs_.push_back({ record.getBoard().getCompactBoard(), move });

    // 1$B<j?J$a$k(B
    if (!record.makeMove()) {
      break;
    }
  }

  return true;
}

/**
 * $B5!3#3X=,$r<B9T$7$^$9!#(B
 */
bool OnlineLearning::run() {
  Loggers::message << "begin learning";

  timer_.set();

  // csa $B%U%!%$%k$rNs5s(B
  FileList fileList;
  std::string dir = config_.getString(LCONF_KIFU);
  fileList.enumerate(dir.c_str(), "csa");

  // $B=i4|2=(B
  eval_.init();
  miniBatchCount_ = 1;
  g_.init();
  w_.init();
  u_.init();

  // $B3X=,%9%l%C%I?t(B
  nt_ = config_.getInt(LCONF_THREADS);

  // Searcher$B@8@.(B
  uint32_t seed = static_cast<uint32_t>(time(NULL));
  rgens_.clear();
  searchers_.clear();
  for (uint32_t wn = 0; wn < nt_; wn++) {
    rgens_.emplace_back(seed);
    seed = rgens_.back()();
    searchers_.emplace_back(new Searcher(eval_));

    auto searchConfig = searchers_.back()->getConfig();
    searchConfig.maxDepth = config_.getInt(LCONF_DEPTH);
    searchConfig.workerSize = 1;
    searchConfig.treeSize = Searcher::standardTreeSize(searchConfig.workerSize);
    searchConfig.enableLimit = false;
    searchConfig.enableTimeManagement = false;
    searchConfig.ponder = false;
    searchConfig.logging = false;
    searchConfig.learning = true;
    searchers_.back()->setConfig(searchConfig);
  }

  // $B4}Ih$N<h$j9~$_(B
  size_t count = 0;
  for (const auto& filename : fileList) {
    readCsa(++count, fileList.size(), filename.c_str());
  }

  // $B71N}%G!<%?$N%7%c%C%U%k(B
  std::shuffle(jobs_.begin(), jobs_.end(), rgens_[0]);

  activeCount_ = 0;

  // $B%o!<%+!<%9%l%C%I@8@.(B
  shutdown_ = false;
  threads_.clear();
  for (uint32_t wn = 0; wn < nt_; wn++) {
    threads_.emplace_back(std::bind(std::mem_fn(&OnlineLearning::work), this, wn));
  }

  // $B3X=,=hM}$N<B9T(B
  while (true) {
    bool ok = miniBatch();
    if (!ok) {
      break;
    }
  }

  // $B%o!<%+!<%9%l%C%IDd;_(B
  shutdown_ = true;
  for (uint32_t wn = 0; wn < nt_; wn++) {
    threads_[wn].join();
  }

  Loggers::message << "completed..";

  float elapsed = timer_.get();
  Loggers::message << "elapsed: " << elapsed;
  Loggers::message << "end learning";

  return true;
}

} // namespace sunfish

#endif // NLEARN
