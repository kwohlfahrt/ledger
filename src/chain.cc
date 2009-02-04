/*
 * Copyright (c) 2003-2009, John Wiegley.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * - Neither the name of New Artisans LLC nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "chain.h"
#include "filters.h"
#include "reconcile.h"

namespace ledger {

xact_handler_ptr chain_xact_handlers(report_t&	      report,
				     xact_handler_ptr base_handler,
				     const bool	      handle_individual_xacts)
{
  bool remember_components = false;

  xact_handler_ptr handler(base_handler);

  // format_xacts write each xact received to the
  // output stream.
  if (handle_individual_xacts) {
    // truncate_entries cuts off a certain number of _entries_ from
    // being displayed.  It does not affect calculation.
    if (report.head_entries || report.tail_entries)
      handler.reset(new truncate_entries(handler, report.head_entries,
					 report.tail_entries));

    // filter_xacts will only pass through xacts matching the
    // `display_predicate'.
    if (! report.display_predicate.empty())
      handler.reset(new filter_xacts
		    (handler, item_predicate<xact_t>(report.display_predicate,
						     report.what_to_keep)));

    // calc_xacts computes the running total.  When this
    // appears will determine, for example, whether filtered
    // xacts are included or excluded from the running total.
    handler.reset(new calc_xacts(handler));

    // component_xacts looks for reported xact that
    // match the given `descend_expr', and then reports the
    // xacts which made up the total for that reported
    // xact.
    if (! report.descend_expr.empty()) {
      std::list<std::string> descend_exprs;

      std::string::size_type beg = 0;
      for (std::string::size_type pos = report.descend_expr.find(';');
	   pos != std::string::npos;
	   beg = pos + 1, pos = report.descend_expr.find(';', beg))
	descend_exprs.push_back(std::string(report.descend_expr,
					    beg, pos - beg));
      descend_exprs.push_back(std::string(report.descend_expr, beg));

      for (std::list<std::string>::reverse_iterator i =
	     descend_exprs.rbegin();
	   i != descend_exprs.rend();
	   i++)
	handler.reset(new component_xacts
		      (handler,
		       item_predicate<xact_t>(*i, report.what_to_keep)));

      remember_components = true;
    }

    // reconcile_xacts will pass through only those
    // xacts which can be reconciled to a given balance
    // (calculated against the xacts which it receives).
    if (! report.reconcile_balance.empty()) {
      date_t cutoff = CURRENT_DATE();
      if (! report.reconcile_date.empty())
	cutoff = parse_date(report.reconcile_date);
      handler.reset(new reconcile_xacts
		    (handler, value_t(report.reconcile_balance), cutoff));
    }

    // filter_xacts will only pass through xacts
    // matching the `secondary_predicate'.
    if (! report.secondary_predicate.empty())
      handler.reset(new filter_xacts
		    (handler, item_predicate<xact_t>(report.secondary_predicate,
						     report.what_to_keep)));

    // sort_xacts will sort all the xacts it sees, based
    // on the `sort_order' value expression.
    if (! report.sort_string.empty()) {
      if (report.entry_sort)
	handler.reset(new sort_entries(handler, report.sort_string));
      else
	handler.reset(new sort_xacts(handler, report.sort_string));
    }

    // changed_value_xacts adds virtual xacts to the
    // list to account for changes in market value of commodities,
    // which otherwise would affect the running total unpredictably.
    if (report.show_revalued)
      handler.reset(new changed_value_xacts(handler, report.total_expr,
					    report.show_revalued_only));

    // collapse_xacts causes entries with multiple xacts
    // to appear as entries with a subtotaled xact for each
    // commodity used.
    if (report.show_collapsed)
      handler.reset(new collapse_xacts(handler, report.session));

    // subtotal_xacts combines all the xacts it receives
    // into one subtotal entry, which has one xact for each
    // commodity in each account.
    //
    // period_xacts is like subtotal_xacts, but it
    // subtotals according to time periods rather than totalling
    // everything.
    //
    // dow_xacts is like period_xacts, except that it
    // reports all the xacts that fall on each subsequent day
    // of the week.
    if (report.show_subtotal)
      handler.reset(new subtotal_xacts(handler, remember_components));

    if (report.days_of_the_week)
      handler.reset(new dow_xacts(handler, remember_components));
    else if (report.by_payee)
      handler.reset(new by_payee_xacts(handler, remember_components));

    // interval_xacts groups xacts together based on a
    // time period, such as weekly or monthly.
    if (! report.report_period.empty()) {
      handler.reset(new interval_xacts(handler, report.report_period,
				       remember_components));
      handler.reset(new sort_xacts(handler, "d"));
    }
  }

  // invert_xacts inverts the value of the xacts it
  // receives.
  if (report.show_inverted)
    handler.reset(new invert_xacts(handler));

  // related_xacts will pass along all xacts related
  // to the xact received.  If `show_all_related' is true,
  // then all the entry's xacts are passed; meaning that if
  // one xact of an entry is to be printed, all the
  // xact for that entry will be printed.
  if (report.show_related)
    handler.reset(new related_xacts(handler, report.show_all_related));

  // anonymize_xacts removes all meaningful information from entry
  // payee's and account names, for the sake of creating useful bug
  // reports.
  if (report.anonymize)
    handler.reset(new anonymize_xacts(handler));

  // This filter_xacts will only pass through xacts
  // matching the `predicate'.
  if (! report.predicate.empty()) {
    DEBUG("report.predicate",
	  "Report predicate expression = " << report.predicate);
    handler.reset(new filter_xacts
		  (handler, item_predicate<xact_t>(report.predicate,
						   report.what_to_keep)));
  }

#if 0
  // budget_xacts takes a set of xacts from a data
  // file and uses them to generate "budget xacts" which
  // balance against the reported xacts.
  //
  // forecast_xacts is a lot like budget_xacts, except
  // that it adds entries only for the future, and does not balance
  // them against anything but the future balance.

  if (report.budget_flags) {
    budget_xacts * budget_handler = new budget_xacts(handler,
						     report.budget_flags);
    budget_handler->add_period_entries(journal->period_entries);
    handler.reset(budget_handler);

    // Apply this before the budget handler, so that only matching
    // xacts are calculated toward the budget.  The use of
    // filter_xacts above will further clean the results so
    // that no automated xacts that don't match the filter get
    // reported.
    if (! report.predicate.empty())
      handler.reset(new filter_xacts(handler, report.predicate));
  }
  else if (! report.forecast_limit.empty()) {
    forecast_xacts * forecast_handler
      = new forecast_xacts(handler, report.forecast_limit);
    forecast_handler->add_period_entries(journal->period_entries);
    handler.reset(forecast_handler);

    // See above, under budget_xacts.
    if (! report.predicate.empty())
      handler.reset(new filter_xacts(handler, report.predicate));
  }
#endif

  if (report.comm_as_payee)
    handler.reset(new set_comm_as_payee(handler));
  else if (report.code_as_payee)
    handler.reset(new set_code_as_payee(handler));

  return handler;
}

} // namespace ledger
