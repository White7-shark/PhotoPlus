import { t as tx } from '../../i18n';
/**
 * Photo Plus Premium plans. One Google Play subscription with three base
 * plans, each opening with a 7-day free trial (a Play offer). Prices are
 * shown from here until Play Billing is connected; Play's own localised
 * prices replace them then.
 *
 * Monthly is GHS 50. Quarterly and yearly are placeholders, to be confirmed
 * before release (PRICES_CONFIRMED).
 */
export const PRICES_CONFIRMED = false;

/** The Play Console subscription and offer the plans belong to. */
export const SUBSCRIPTION_ID = 'photoplus_premium';
export const TRIAL_OFFER_ID = 'free-trial-7d';
export const TRIAL_DAYS = 7;

export type PlanId = 'monthly' | 'quarterly' | 'yearly';

export interface PremiumPlan {
  id: PlanId;
  /** The Play base plan id. */
  basePlanId: string;
  name: string;
  months: number;
  /** The plan's price for its whole period. */
  price: number;
  currency: 'GHS';
  /** A highlight on the plan card. */
  badge?: string;
}

export const PLANS: readonly PremiumPlan[] = [
  { id: 'monthly', basePlanId: 'monthly', name: tx('plans_monthly'), months: 1, price: 50, currency: 'GHS' },
  {
    id: 'quarterly',
    basePlanId: 'quarterly',
    name: tx('plans_3_months'),
    months: 3,
    price: 135,
    currency: 'GHS',
    badge: tx('plans_popular'),
  },
  {
    id: 'yearly',
    basePlanId: 'yearly',
    name: tx('plans_yearly'),
    months: 12,
    price: 480,
    currency: 'GHS',
    badge: tx('plans_best_value'),
  },
];

export const plan = (id: PlanId) => PLANS.find(p => p.id === id) ?? PLANS[0];
const MONTHLY = PLANS[0];

/** The plan's price per month, rounded to the cent. */
export const perMonth = (p: PremiumPlan) => Math.round((p.price / p.months) * 100) / 100;

/** What the plan saves against paying monthly, in whole percent. */
export const savings = (p: PremiumPlan) => Math.round((1 - p.price / (MONTHLY.price * p.months)) * 100);

export const money = (amount: number, currency = 'GHS') =>
  `${currency} ${Number.isInteger(amount) ? amount : amount.toFixed(2)}`;

export const periodLabel = (p: PremiumPlan) =>
  p.months === 1 ? 'month' : p.months === 12 ? 'year' : `${p.months} months`;
