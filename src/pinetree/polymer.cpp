#include "polymer.hpp"
#include "IntervalTree.h"
#include "choices.hpp"
#include "tracker.hpp"

#include <iostream>

Polymer::Polymer(const std::string &name, int start, int stop)
    : name_(name), start_(start), stop_(stop) {
  weights_ = std::vector<double>(stop - start + 1, 1.0);

  std::map<std::string, double> interaction_map;
  mask_ = Mask("mask", stop_ + 1, stop_, interaction_map);
}

void Polymer::Initialize() {
  // Construct invterval trees
  binding_sites_ = IntervalTree<Promoter::Ptr>(binding_intervals_);
  release_sites_ = IntervalTree<Terminator::Ptr>(release_intervals_);
  std::vector<Interval<Promoter::Ptr>> results;

  // Cover all masked sites
  int mask_start = mask_.start();
  int mask_stop = mask_.stop();
  binding_sites_.findOverlapping(mask_start, mask_stop, results);
  for (auto &interval : results) {
    CoverBindingSite(interval.value->name());
    interval.value->Cover();
    interval.value->SaveState();
  }

  // Make sure all unmasked sites are uncovered
  binding_sites_.findOverlapping(start_, mask_stop, results);
  for (auto &interval : results) {
    UncoverBindingSite(interval.value->name());
    interval.value->Uncover();
    interval.value->SaveState();
  }
}

void Polymer::Bind(Polymerase::Ptr pol, const std::string &promoter_name) {
  // Make a list of free promoters that pol can bind
  bool found = false;
  Promoter::VecPtr promoter_choices;
  std::vector<Interval<Promoter::Ptr>> results;
  binding_sites_.findOverlapping(start_, mask_.start(), results);
  for (auto &interval : results) {
    if (interval.value->name() == promoter_name &&
        !interval.value->IsCovered()) {
      promoter_choices.push_back(interval.value);
      found = true;
    }
  }
  // Error checking
  if (!found) {
    std::string err = "Polymerase " + pol->name() +
                      " could not find free promoter " + promoter_name +
                      " to bind in the polyemr " + name_;
    throw std::runtime_error(err);
  }
  // Randomly select promoter.
  Promoter::Ptr elem = Random::WeightedChoice(promoter_choices);
  // More error checking.
  if (!elem->CheckInteraction(pol->name())) {
    std::string err = "Polymerase " + pol->name() +
                      " does not interact with promoter " + promoter_name;
    throw std::runtime_error(err);
  }
  // Update polymerase coordinates
  // (TODO: refactor; pol doesn't need to expose footprint/stop position)
  pol->set_start(elem->start());
  pol->set_stop(elem->start() + pol->footprint() - 1);
  // More error checking.
  if (pol->stop() >= mask_.start()) {
    std::string err = "Polymerase " + pol->name() +
                      " will overlap with mask upon promoter binding. This may "
                      "cause the polymerase to stall and produce unexpected "
                      "behavior.";
    throw std::runtime_error(err);
  }
  elem->Cover();
  elem->SaveState();
  // Cover promoter in cache
  CoverBindingSite(elem->name());
  // Add polymerase to this polymer
  Insert(pol);

  // Report some data to tracker
  if (elem->interactions().count("ribosome") == 1 &&
      elem->type() == "promoter") {
    auto &tracker = SpeciesTracker::Instance();
    tracker.IncrementRibo(elem->gene(), 1);
  }
}

void Polymer::Execute() {
  if (prop_sum_ == 0) {
    throw std::runtime_error(
        "Attempting to execute polymer with reaction propensity of 0.");
  }
  int pol_index = Choose();
  species_log_.clear();
  Move(pol_index);
}

void Polymer::ShiftMask() {
  if (mask_.start() <= mask_.stop()) {
    int old_start = mask_.start();
    mask_.Recede();

    CheckBehind(old_start, mask_.start());
  }
}

void Polymer::Terminate(Polymerase::Ptr pol, const std::string &last_gene) {
  auto it = std::find(polymerases_.begin(), polymerases_.end(), pol);
  int index = it - polymerases_.begin();
  prop_sum_ -= prop_list_[index];
  termination_signal_.Emit(index_, pol->name(), last_gene);
  polymerases_.erase(it);
  prop_list_.erase(prop_list_.begin() + index);
  if (prop_list_.size() != polymerases_.size()) {
    throw std::runtime_error("Prop list not correct size.");
  }
}

void Polymer::CoverBindingSite(const std::string &species_name) {
  if (uncovered_.count(species_name) == 0) {
    uncovered_[species_name] = 0;
  } else {
    uncovered_[species_name]--;
  }
  if (uncovered_[species_name] < 0) {
    std::string err = "Cached count of uncovered element " + species_name +
                      " cannot be a negative value";
    throw std::runtime_error(err);
  }
  if (species_log_.count(species_name) == 0) {
    species_log_[species_name] = -1;
  } else {
    species_log_[species_name]--;
  }
}

void Polymer::UncoverBindingSite(const std::string &species_name) {
  if (uncovered_.count(species_name) == 0) {
    uncovered_[species_name] = 1;
  } else {
    uncovered_[species_name]++;
  }
  if (species_log_.count(species_name) == 0) {
    species_log_[species_name] = 1;
  } else {
    species_log_[species_name]++;
  }
}

void Polymer::Insert(Polymerase::Ptr pol) {
  // Find where in vector polymerase should insert; use a lambda function
  // to make comparison between polymerase pointers
  auto it = std::upper_bound(polymerases_.begin(), polymerases_.end(), pol,
                             [](Polymerase::Ptr a, Polymerase::Ptr b) {
                               return a->start() < b->start();
                             });
  // Record position for prop_list_
  // NOTE: iterators become invalid as soon as a vector is changed!!
  // Attempting to use an iterator twice will lead to a segfault.
  auto prop_it = (it - polymerases_.begin()) + prop_list_.begin();
  // Add polymerase to this polymer
  polymerases_.insert(it, pol);
  // Cache polymerase speed
  double weight = weights_[pol->stop() - start_ - 1];
  // Update total move propensity of this polymer
  prop_sum_ += weight * pol->speed();
  prop_list_.insert(prop_it, weight * pol->speed());
  if (prop_list_.size() != polymerases_.size()) {
    throw std::runtime_error("Prop list not correct size.");
  }
}

int Polymer::Choose() {
  if (prop_list_.size() == 0) {
    std::string err = "There are no active polymerases on polymer " + name_ +
                      std::to_string(prop_sum_);
    throw std::runtime_error(err);
  }
  int pol_index = Random::WeightedChoiceIndex(polymerases_, prop_list_);
  // Error checking to make sure that pol is in vector
  if (pol_index >= polymerases_.size()) {
    std::string err = "Attempting to move unbound polymerase with index " +
                      std::to_string(pol_index) + " on polymer " + name_;
    throw std::runtime_error(err);
  }
  if (pol_index >= prop_list_.size()) {
    throw std::runtime_error(
        "Prop list vector index is invalid (before move).");
  }
  return pol_index;
}

void Polymer::Move(int pol_index) {
  if (pol_index >= prop_list_.size()) {
    throw std::runtime_error("Prop list vector index is invalid.");
  }
  auto pol = polymerases_[pol_index];

  // Record old positions
  int old_start = pol->start();
  int old_stop = pol->stop();

  // Move polymerase
  pol->Move();

  // Check for upstream polymerase collision
  bool pol_collision = CheckPolCollisions(pol_index);
  if (pol_collision) {
    pol->MoveBack();
    return;
  }

  // Check for collisions with mask
  bool mask_collision = CheckMaskCollisions(pol);
  if (mask_collision) {
    pol->MoveBack();
    return;
  }
  // Check if polymerase has run into a terminator
  bool terminating = CheckTermination(pol);
  if (terminating) {
    // Log some species changes
    // Uncover all elements
    return;
  }

  // Check for new covered and uncovered elements
  CheckBehind(old_start, pol->start());
  CheckAhead(old_stop, pol->stop());

  // Update propensity for new codon (TODO: make its own function)
  if ((pol->stop() - start_ - 1) >= weights_.size()) {
    throw std::runtime_error("Weight is missing for this position.");
  }
  double weight = weights_[pol->stop() - start_ - 1];
  double new_speed = weight * pol->speed();
  double diff = new_speed - prop_list_[pol_index];
  prop_sum_ += diff;
  prop_list_[pol_index] = new_speed;
}

void Polymer::CheckAhead(int old_stop, int new_stop) {
  std::vector<Interval<Promoter::Ptr>> results;
  binding_sites_.findOverlapping(old_stop, new_stop, results);
  for (auto &interval : results) {
    if (interval.value->start() < new_stop) {
      interval.value->Cover();
      if (interval.value->WasCovered()) {
        // Record changes that species was covered
      }
    }
  }
}

void Polymer::CheckBehind(int old_start, int new_start) {
  std::vector<Interval<Promoter::Ptr>> results;
  binding_sites_.findOverlapping(old_start, new_start, results);
  for (auto &interval : results) {
    if (interval.value->stop() < new_start) {
      interval.value->Uncover();
      if (interval.value->WasUncovered()) {
        // Record changes that species was covered
      }
    }
  }
}

bool Polymer::CheckTermination(Polymerase::Ptr pol) {
  std::vector<Interval<Terminator::Ptr>> results;
  release_sites_.findOverlapping(pol->start(), pol->stop(), results);
  for (auto &interval : results) {
    if (interval.value->CheckInteraction(pol->name(), pol->reading_frame()) &&
        !interval.value->readthrough()) {
      // terminate
      double random_num = Random::random();
      if (random_num <= interval.value->efficiency(pol->name())) {
        // Fire Emit signal until entire terminator is uncovered
        // Coordinates are inclusive, so must add 1 after calculating
        // difference
        int dist = interval.value->stop() - pol->stop() + 1;
        for (int i = 0; i < dist; i++) {
          pol->move_signal_.Emit();
        }
        Terminate(pol, interval.value->gene());
        return true;
      } else {
        interval.value->set_readthrough(true);
      }
    }
  }
  return false;
}

bool Polymer::CheckMaskCollisions(Polymerase::Ptr pol) {
  // Is there still a mask, and does it overlap polymerase?
  if (mask_.start() <= stop_ && pol->stop() >= mask_.start()) {
    if (pol->stop() - mask_.start() > 0) {
      std::string err =
          "Polymerase " + pol->name() +
          " is overlapping mask by more than one position on polymer";
      throw std::runtime_error(err);
    }
    if (mask_.CheckInteraction(pol->name())) {
      ShiftMask();
    } else {
      return true;
    }
  }
  return false;
}

bool Polymer::CheckPolCollisions(int pol_index) {
  // Are there any polymerases ahead of this one?
  if (pol_index + 1 >= polymerases_.size()) {
    return false;
  }
  // We only need to check the polymerase one position ahead of this polymerase
  if ((polymerases_[pol_index]->stop() >=
       polymerases_[pol_index + 1]->start()) &&
      (polymerases_[pol_index + 1]->stop() >=
       polymerases_[pol_index]->start())) {
    // Error checking. TODO: Can this be removed?
    if (polymerases_[pol_index]->stop() >
        polymerases_[pol_index + 1]->start()) {
      std::string err =
          "Polymerase " + polymerases_[pol_index]->name() +
          " (start: " + std::to_string(polymerases_[pol_index]->start()) +
          ", stop: " + std::to_string(polymerases_[pol_index]->stop()) +
          ", index: " + std::to_string(pol_index) +
          ") is overlapping polymerase " + polymerases_[pol_index + 1]->name() +
          " (start: " + std::to_string(polymerases_[pol_index + 1]->start()) +
          ", stop: " + std::to_string(polymerases_[pol_index + 1]->stop()) +
          ", index: " + std::to_string(pol_index + 1) +
          ") by more than one position on polymer " + name_;
      throw std::runtime_error(err);
    }
    return true;
  }
  return false;
}

Transcript::Transcript(
    const std::string &name, int start, int stop,
    const std::vector<Interval<Promoter::Ptr>> &rbs_intervals,
    const std::vector<Interval<Terminator::Ptr>> &stop_site_intervals,
    const Mask &mask, const std::vector<double> &weights)
    : Polymer(name, start, stop) {
  mask_ = mask;
  weights_ = weights;
  binding_intervals_ = rbs_intervals;
  release_intervals_ = stop_site_intervals;
}

void Transcript::Bind(Polymerase::Ptr pol, const std::string &promoter_name) {
  // Bind polymerase just like in parent Polymer
  Polymer::Bind(pol, promoter_name);
  // Set the reading frame of the polymerase
  // TODO: should the reading frame be set by the polymerase start position?
  pol->set_reading_frame(pol->start() % 3);
}

Genome::Genome(const std::string &name, int length) : Polymer(name, 1, length) {
  transcript_weights_ = std::vector<double>(length, 1.0);
}

void Genome::Initialize() {
  Polymer::Initialize();
  transcript_rbs_ = IntervalTree<Promoter::Ptr>(transcript_rbs_intervals_);
  transcript_stop_sites_ =
      IntervalTree<Terminator::Ptr>(transcript_stop_site_intervals_);
}

void Genome::AddMask(int start, const std::vector<std::string> &interactions) {
  std::map<std::string, double> interaction_map;
  for (auto name : interactions) {
    interaction_map[name] = 1.0;
  }
  mask_ = Mask("mask", start, stop_, interaction_map);
}

void Genome::AddPromoter(const std::string &name, int start, int stop,
                         const std::map<std::string, double> &interactions) {
  Promoter::Ptr promoter =
      std::make_shared<Promoter>(name, start, stop, interactions);
  binding_intervals_.push_back(Interval<Promoter::Ptr>(start, stop, promoter));
  bindings_[name] = interactions;
}

const std::map<std::string, std::map<std::string, double>> &Genome::bindings() {
  return bindings_;
}

void Genome::AddTerminator(const std::string &name, int start, int stop,
                           const std::map<std::string, double> &efficiency) {
  Terminator::Ptr terminator =
      std::make_shared<Terminator>(name, start, stop, efficiency);
  // New code for IntervalTree
  release_intervals_.push_back(
      Interval<Terminator::Ptr>(start, stop, terminator));
}

// TODO: Add error checking to make sure rbs does not overlap with terminator
void Genome::AddGene(const std::string &name, int start, int stop,
                     int rbs_start, int rbs_stop, double rbs_strength) {
  auto binding = std::map<std::string, double>{{"ribosome", rbs_strength}};
  auto term = std::map<std::string, double>{{"ribosome", 1.0}};
  auto rbs =
      std::make_shared<Promoter>(name + "_rbs", rbs_start, rbs_stop, binding);
  rbs->gene(name);
  transcript_rbs_intervals_.push_back(
      Interval<Promoter::Ptr>(rbs->start(), rbs->stop(), rbs));
  bindings_[name + "_rbs"] = binding;
  auto stop_codon =
      std::make_shared<Terminator>("stop_codon", stop - 1, stop, term);
  stop_codon->set_reading_frame(start % 3);
  stop_codon->gene(name);
  transcript_stop_site_intervals_.push_back(Interval<Terminator::Ptr>(
      stop_codon->start(), stop_codon->stop(), stop_codon));
}

void Genome::AddWeights(const std::vector<double> &transcript_weights) {
  if (transcript_weights.size() != (stop_ - start_ + 1)) {
    throw std::length_error("Weights vector is not the correct size. " +
                            std::to_string(transcript_weights.size()) + " " +
                            std::to_string(stop_ - start_ + 1));
  }
  transcript_weights_ = transcript_weights;
}

void Genome::Bind(Polymerase::Ptr pol, const std::string &promoter_name) {
  // Bind polymerase
  Polymer::Bind(pol, promoter_name);
  // Construct a transcript starting from *end* of polymerase
  Transcript::Ptr transcript = BuildTranscript(pol->stop(), stop_);
  // Connect polymerase movement signal to transcript, so that transcript knows
  // when to expose new elements
  // TODO: figure out if this could cause a memory leak
  // What happens if transcript gets deleted?
  pol->move_signal_.ConnectMember(transcript, &Transcript::ShiftMask);
  // Fire new transcript signal (adds transcript to Simulation)
  transcript_signal_.Emit(transcript);
}

Transcript::Ptr Genome::BuildTranscript(int start, int stop) {
  std::vector<Interval<Promoter::Ptr>> prom_results;
  std::vector<Interval<Promoter::Ptr>> rbs_intervals;
  transcript_rbs_.findContained(start, stop, prom_results);
  for (auto &interval : prom_results) {
    int start = interval.start;
    int stop = interval.stop;
    auto elem = interval.value;
    rbs_intervals.emplace_back(
        Interval<Promoter::Ptr>(start, stop, elem->Clone()));
  }

  std::vector<Interval<Terminator::Ptr>> term_results;
  std::vector<Interval<Terminator::Ptr>> stop_site_intervals;
  transcript_stop_sites_.findContained(start, stop, term_results);
  for (auto &interval : term_results) {
    int start = interval.start;
    int stop = interval.stop;
    auto elem = interval.value;
    stop_site_intervals.emplace_back(
        Interval<Terminator::Ptr>(start, stop, elem->Clone()));
  }

  Transcript::Ptr transcript;
  Mask mask = Mask("mask", start, stop, std::map<std::string, double>());
  // We need to used the standard shared_ptr constructor here because the
  // constructor of Transcript needs to know its address in memory to wire
  // signals appropriately.
  transcript = std::make_shared<Transcript>("rna", start_, stop_, rbs_intervals,
                                            stop_site_intervals, mask,
                                            transcript_weights_);
  return transcript;
}