/*
 alignment.c
 author: Isaac Turner <turner.isaac@gmail.com>
 url: https://github.com/noporpoise/seq-align
 May 2013
 */

// Turn on debugging output by defining DEBUG
//#define DEBUG

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h> // INT_MIN
#include <ctype.h> // tolower

#include "alignment.h"

const char align_col_mismatch[] = "\033[92m"; // Mismatch (GREEN)
const char align_col_indel[] = "\033[91m"; // Insertion / deletion (RED)
// Pink used by SmithWaterman local alignment for printing surrounding bases
const char align_col_context[] = "\033[95m";
const char align_col_stop[] = "\033[0m";

// long max4(long a, long b, long c, long d)
// {
//   long result = a;
//   if(b > result) result = b;
//   if(c > result) result = c;
//   if(d > result) result = d;
//   return result;
// }

static score_t nw_combine(long a, long b, long c) {
  return MAX3(a,b,c);
}

static score_t sw_combine(long a, long b, long c) {
  score_t x = MAX3(a,b,c);
  return MAX2(x,0);
}

// Fill in traceback matrix
static void alignment_fill_matrices(aligner_t *aligner, char is_sw)
{
  score_t *match_scores = aligner->match_scores;
  score_t *gap_a_scores = aligner->gap_a_scores;
  score_t *gap_b_scores = aligner->gap_b_scores;
  const scoring_t *scoring = aligner->scoring;
  size_t score_width = aligner->score_width;
  size_t score_height = aligner->score_height;

  size_t i, j, arr_size = score_width * score_height;

  score_t min;
  score_t (*combine)(long a, long b, long c);

  if(is_sw) {
    min = 0;
    combine = sw_combine;
  } else {
    min = INT_MIN;
    combine = nw_combine;
  }

  if(scoring->no_gaps_in_a) {
    for(i = 0; i < arr_size; i++) gap_a_scores[i] = min;
  }

  if(scoring->no_gaps_in_b) {
    for(i = 0; i < arr_size; i++) gap_b_scores[i] = min;
  }

  // [0][0]
  match_scores[0] = 0;
  gap_a_scores[0] = 0;
  gap_b_scores[0] = 0;

  // work along first row -> [i][0]
  for(i = 1; i < score_width; i++)
  {
    match_scores[i] = min;
    
    // Think carefully about which way round these are
    gap_a_scores[i] = min;
    gap_b_scores[i] = scoring->no_start_gap_penalty ? 0
                      : scoring->gap_open + (long)i * scoring->gap_extend;
  }

  // work down first column -> [0][j]
  for(j = 1; j < score_height; j++)
  {
    size_t index = j * score_width;
    match_scores[index] = min;
    
    // Think carefully about which way round these are
    gap_a_scores[index]
      = (score_t)(scoring->no_start_gap_penalty ? 0
                  : scoring->gap_open + (long)i * scoring->gap_extend);
    gap_b_scores[index] = min;
  }
  
  //
  // Update Dynamic Programming arrays
  //

  // These are longs to force addition to be done with higher accuracy
  long gap_open_penalty = scoring->gap_extend + scoring->gap_open;
  long gap_extend_penalty = scoring->gap_extend;

  for (i = 1; i < score_width; i++)
  {
    for (j = 1; j < score_height; j++)
    {
      // It's an indexing thing...
      size_t seq_i = i - 1, seq_j = j - 1;
      
      // Update match_scores[i][j] with position [i-1][j-1]
      // Addressing array must be done with unsigned long
      size_t old_index, new_index = j*score_width + i;
      
      // substitution penalty
      int substitution_penalty;
      char is_match;

      scoring_lookup(scoring, aligner->seq_a[seq_i], aligner->seq_b[seq_j],
                     &substitution_penalty, &is_match);

      if(scoring->no_mismatches && !is_match)
      {
        match_scores[new_index] = min;
      }
      else
      {
        old_index = ARR_2D_INDEX(score_width, i-1, j-1);

        // substitution
        match_scores[new_index]
          = combine(match_scores[old_index], // continue alignment
                    gap_a_scores[old_index], // close gap in seq_a
                    gap_b_scores[old_index]) // close gap in seq_b
            + substitution_penalty;
      }                                     

      // Long arithmetic since some INTs are set to min and penalty is -ve
      // (adding as ints would cause an integer overflow)

      if(!scoring->no_gaps_in_a)
      {
        // Update gap_a_scores[i][j] from position [i][j-1]
        old_index = ARR_2D_INDEX(score_width, i, j-1);

        if(i == score_width-1 && scoring->no_end_gap_penalty)
        {
          gap_a_scores[new_index]
            = combine(match_scores[old_index],
                      gap_a_scores[old_index],
                      gap_b_scores[old_index] + (j == 1 ? 0 : gap_open_penalty));
        }
        else
        {
          gap_a_scores[new_index]
            = combine(match_scores[old_index] + gap_open_penalty,
                      gap_a_scores[old_index] + gap_extend_penalty,
                      gap_b_scores[old_index] + gap_open_penalty);
        }
      }

      if(!scoring->no_gaps_in_b)
      {
        // Update gap_b_scores[i][j] from position [i-1][j]
        old_index = ARR_2D_INDEX(score_width, i-1, j);
        
        if(j == score_height-1 && scoring->no_end_gap_penalty)
        {
          gap_b_scores[new_index]
            = combine(match_scores[old_index],
                      gap_a_scores[old_index] + (i == 1 ? 0 : gap_open_penalty),
                      gap_b_scores[old_index]);
        }
        else
        {
          gap_b_scores[new_index]
            = combine(match_scores[old_index] + gap_open_penalty,
                      gap_a_scores[old_index] + gap_open_penalty,
                      gap_b_scores[old_index] + gap_extend_penalty);
        }
      }
    }
  }

  if(scoring->no_gaps_in_a)
  {
    // Allow gaps only at the start/end of A
    size_t old_index = ARR_2D_INDEX(score_width, score_width-1, 0);

    for(j = 1; j < score_height; j++)
    {
      size_t new_index = ARR_2D_INDEX(score_width, score_width-1, j);

      gap_a_scores[new_index]
        = combine(match_scores[old_index] + gap_open_penalty,
                  gap_a_scores[old_index] + gap_extend_penalty,
                  min);

      old_index = new_index;
    }
  }

  if(scoring->no_gaps_in_b)
  {
    // Allow gaps only at the start/end of B
    size_t old_index = ARR_2D_INDEX(score_width, 0, score_height-1);

    for(i = 1; i < score_width; i++)
    {
      size_t new_index = ARR_2D_INDEX(score_width, i, score_height-1);

      gap_b_scores[new_index]
        = combine(match_scores[old_index] + gap_open_penalty,
                  gap_b_scores[old_index] + gap_extend_penalty,
                  min);

      old_index = new_index;
    }
  }
}

void aligner_align(aligner_t *aligner, const char *seq_a, const char *seq_b,
                   const scoring_t *scoring, char is_sw)
{
  size_t len_a = strlen(seq_a);
  size_t len_b = strlen(seq_b);

  aligner_t tmp = {.scoring = scoring, .seq_a = seq_a, .seq_b = seq_b,
                   .score_width = len_a+1, .score_height = len_b+1};

  size_t new_capacity = tmp.score_width * tmp.score_height;

  if(aligner->capacity == 0 || aligner->capacity < new_capacity)
  {
    tmp.capacity = ROUNDUP2POW(new_capacity);
    size_t mem = sizeof(score_t) * tmp.capacity;

    if(aligner->capacity == 0) {
      tmp.match_scores = malloc(mem);
      tmp.gap_a_scores = malloc(mem);
      tmp.gap_b_scores = malloc(mem);
    } else {
      tmp.match_scores = realloc(aligner->match_scores, mem);
      tmp.gap_a_scores = realloc(aligner->gap_a_scores, mem);
      tmp.gap_b_scores = realloc(aligner->gap_b_scores, mem);
    }
  } else {
    tmp.capacity = aligner->capacity;
    tmp.match_scores = aligner->match_scores;
    tmp.gap_a_scores = aligner->gap_a_scores;
    tmp.gap_b_scores = aligner->gap_b_scores;
  }

  memcpy(aligner, &tmp, sizeof(aligner_t));

  alignment_fill_matrices(aligner, is_sw);
}

void aligner_destroy(aligner_t *aligner)
{
  if(aligner->capacity > 0) {
    free(aligner->match_scores);
    free(aligner->gap_a_scores);
    free(aligner->gap_b_scores);
  }
}


alignment_t* alignment_create(size_t capacity)
{
  capacity = ROUNDUP2POW(capacity);
  alignment_t *result = malloc(sizeof(alignment_t));
  result->result_a = malloc(sizeof(char)*capacity);
  result->result_b = malloc(sizeof(char)*capacity);
  result->capacity = capacity;
  result->length = 0;
  result->result_a[0] = result->result_b[0] = '\0';
  result->pos_a = result->pos_b = result->len_a = result->len_b = 0;
  result->score = 0;
  return result;
}

void alignment_ensure_capacity(alignment_t* result, size_t strlength)
{
  size_t capacity = strlength+1;
  if(result->capacity < capacity)
  {
    capacity = ROUNDUP2POW(capacity);
    result->result_a = realloc(result->result_a, sizeof(char)*capacity);
    result->result_b = realloc(result->result_b, sizeof(char)*capacity);
    result->capacity = capacity;
    if(result->result_a == NULL || result->result_b == NULL) {
      fprintf(stderr, "%s:%i: Out of memory\n", __FILE__, __LINE__);
      exit(EXIT_FAILURE);
    }
  }
}

void alignment_free(alignment_t* result)
{
  free(result->result_a);
  free(result->result_b);
}


// Backtrack through scoring matrices
void alignment_reverse_move(enum Matrix *curr_matrix, score_t *curr_score,
                            size_t *score_x, size_t *score_y,
                            size_t *arr_index, const aligner_t *aligner)
{
  int prev_match_penalty, prev_gap_a_penalty, prev_gap_b_penalty;

  size_t seq_x = (*score_x)-1;
  size_t seq_y = (*score_y)-1;

  char is_match;
  int match_penalty;
  const scoring_t *scoring = aligner->scoring;

  scoring_lookup(scoring, aligner->seq_a[seq_x], aligner->seq_b[seq_y],
                 &match_penalty, &is_match);

  int gap_open_penalty = scoring->gap_extend + scoring->gap_open;
  int gap_extend_penalty = scoring->gap_extend;

  if(scoring->no_end_gap_penalty &&
     (*score_x == aligner->score_width-1 || *score_y == aligner->score_height-1))
  {
    gap_open_penalty = 0;
    gap_extend_penalty = 0;
  }

  switch(*curr_matrix)
  {
    case MATCH:
      prev_match_penalty = match_penalty;
      prev_gap_a_penalty = match_penalty;
      prev_gap_b_penalty = match_penalty;
      (*score_x)--;
      (*score_y)--;
      break;

    case GAP_A:
      prev_match_penalty = gap_open_penalty;
      prev_gap_a_penalty = gap_extend_penalty;
      prev_gap_b_penalty = gap_open_penalty;
      (*score_y)--;
      break;

    case GAP_B:
      prev_match_penalty = gap_open_penalty;
      prev_gap_a_penalty = gap_open_penalty;
      prev_gap_b_penalty = gap_extend_penalty;
      (*score_x)--;
      break;

    default:
      fprintf(stderr, "Program error: invalid matrix in get_reverse_move()\n");
      fprintf(stderr, "Please submit a bug report to: turner.isaac@gmail.com\n");
      exit(EXIT_FAILURE);
  }

  *arr_index = ARR_2D_INDEX(aligner->score_width, *score_x, *score_y);

  if((!scoring->no_gaps_in_a || *score_x == 0 || *score_x == aligner->score_width-1) &&
     (long)aligner->gap_a_scores[*arr_index] + prev_gap_a_penalty == *curr_score)
  {
    *curr_matrix = GAP_A;
    *curr_score = aligner->gap_a_scores[*arr_index];
    return;
  }
  else if((!scoring->no_gaps_in_b || *score_y == 0 || *score_y == aligner->score_height-1) &&
          (long)aligner->gap_b_scores[*arr_index] + prev_gap_b_penalty == *curr_score)
  {
    *curr_matrix = GAP_B;
    *curr_score = aligner->gap_b_scores[*arr_index];
  }
  else if((!scoring->no_mismatches || is_match) &&
          (long)aligner->match_scores[*arr_index] + prev_match_penalty == *curr_score)
  {
    *curr_matrix = MATCH;
    *curr_score = aligner->match_scores[*arr_index];
  }
  else
  {
    fprintf(stderr, "Program error: traceback fail (get_reverse_move)\n");
    fprintf(stderr, "This may be due to an integer overflow if your "
                    "sequences are long or if scores are large.  \n");
    fprintf(stderr, "If this is the case using smaller scores or "
                    "shorter sequences may work around this problem.  \n");
    fprintf(stderr, " If you think this is a bug, please report it to: "
                    "turner.isaac@gmail.com\n");
    exit(EXIT_FAILURE);
  }
}


void alignment_print_matrices(const aligner_t *aligner)
{
  const score_t* match_scores = aligner->match_scores;
  const score_t* gap_a_scores = aligner->gap_a_scores;
  const score_t* gap_b_scores = aligner->gap_b_scores;

  size_t i, j;

  printf("match_scores:\n");
  for(j = 0; j < aligner->score_height; j++)
  {
    printf("%3i:", (int)j);
    for(i = 0; i < aligner->score_width; i++)
    {
      printf(" %3i", (int)ARR_LOOKUP(match_scores, aligner->score_width, i, j));
    }
    printf("\n");
  }
  printf("gap_a_scores:\n");
  for(j = 0; j < aligner->score_height; j++)
  {
    printf("%3i:", (int)j);
    for(i = 0; i < aligner->score_width; i++)
    {
      printf(" %3i", (int)ARR_LOOKUP(gap_a_scores, aligner->score_width, i, j));
    }
    printf("\n");
  }
  printf("gap_b_scores:\n");
  for(j = 0; j < aligner->score_height; j++)
  {
    printf("%3i:", (int)j);
    for(i = 0; i < aligner->score_width; i++)
    {
      printf(" %3i", (int)ARR_LOOKUP(gap_b_scores, aligner->score_width, i, j));
    }
    printf("\n");
  }
}

void alignment_colour_print_against(const char *alignment_a,
                                    const char *alignment_b,
                                    char case_sensitive)
{
  int i;
  char red = 0, green = 0;

  for(i = 0; alignment_a[i] != '\0'; i++)
  {
    if(alignment_b[i] == '-')
    {
      if(!red)
      {
        printf("%s", align_col_indel);
        red = 1;
      }
    }
    else if(red)
    {
      red = 0;
      printf("%s", align_col_stop);
    }

    if(((case_sensitive && alignment_a[i] != alignment_b[i]) ||
        (!case_sensitive && tolower(alignment_a[i]) != tolower(alignment_b[i]))) &&
       alignment_a[i] != '-' && alignment_b[i] != '-')
    {
      if(!green)
      {
        printf("%s", align_col_mismatch);
        green = 1;
      }
    }
    else if(green)
    {
      green = 0;
      printf("%s", align_col_stop);
    }

    printf("%c", alignment_a[i]);
  }

  if(green || red)
  {
    // Stop all colours
    printf("%s", align_col_stop);
  }
}

// Order of alignment_a / alignment_b is not important
void alignment_print_spacer(const char* alignment_a, const char* alignment_b,
                            const scoring_t* scoring)
{
  int i;

  for(i = 0; alignment_a[i] != '\0'; i++)
  {
    if(alignment_a[i] == '-' || alignment_b[i] == '-')
    {
      printf(" ");
    }
    else if(alignment_a[i] == alignment_b[i] ||
            (!scoring->case_sensitive &&
             tolower(alignment_a[i]) == tolower(alignment_b[i])))
    {
      printf("|");
    }
    else
    {
      printf("*");
    }
  }
}
