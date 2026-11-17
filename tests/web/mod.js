export const greet = (name) => `Bonjour ${name} depuis un module ES !`;
export default class Counter { #n = 0; inc() { return ++this.#n; } }
